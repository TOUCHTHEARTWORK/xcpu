/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <libspe2.h>
#include "spfs.h"
#include "spfsimpl.h"

typedef struct Spcbesrv Spcbesrv;
typedef struct Spcbeconn Spcbeconn;
typedef struct Spcbereq Spcbereq;

enum {
	Rfree,
	Rrdma,
	Rwork,
	Rwdma,
};

struct Spcbereq {
	Spreq*		req;
	int		state;
	u16		tag;
	u32		lsptr;
};
	
struct Spcbeconn {
	Spconn*			conn;
	int			stopped;
	spe_context_ptr_t	speid;
	spe_event_unit_t	ev;
	Spcbereq		reqs[8];
};

struct Spcbesrv {
	spe_event_handler_ptr_t	evhandler;
	Spsrv*			srv;
};

static int sp_cbeconn_shutdown(Spconn *conn);
static void sp_cbeconn_dataout(Spconn *conn, Spreq *req);
static void sp_cbesrv_start(Spsrv *srv);
static void sp_cbesrv_shutdown(Spsrv *srv);
static void sp_cbesrv_destroy(Spsrv *srv);
static Spfcall *sp_cbeconn_fcall_alloc(Spconn *conn);
static void sp_cbeconn_fcall_free(Spconn *conn, Spfcall *fc);

Spconn*
sp_cbeconn_create(Spsrv *srv, spe_context_ptr_t speid)
{
	int i;
	Spconn *conn;
	Spcbeconn *cbeconn;
	Spcbesrv *csrv;

	csrv = srv->srvaux;
	conn = sp_conn_create(srv);
	if (!conn)
		return NULL;

	cbeconn = sp_malloc(sizeof(*cbeconn));
	if (!cbeconn)
		goto error;

	cbeconn->speid = speid;
	for(i = 0; i < 8; i++) {
		cbeconn->reqs[i].state = Rfree;
		cbeconn->reqs[i].req = NULL;
		cbeconn->reqs[i].tag = i;
		cbeconn->reqs[i].lsptr = 0;
	}

	conn->caux = cbeconn;
	conn->shutdown = sp_cbeconn_shutdown;
	conn->dataout = sp_cbeconn_dataout;
	sp_srv_add_conn(srv, conn);

	cbeconn->conn = conn;
	cbeconn->stopped = 0;
	cbeconn->ev.spe = speid;
	cbeconn->ev.events = SPE_EVENT_OUT_INTR_MBOX | SPE_EVENT_TAG_GROUP | SPE_EVENT_SPE_STOPPED;
	cbeconn->ev.data.ptr = cbeconn;
	if (spe_event_handler_register(csrv->evhandler, &cbeconn->ev)) {
		sp_werror("can't register event", EIO);
		fprintf(stderr, "error\n");
		goto error;
	}

	return conn;

error:
	free(cbeconn);
	sp_conn_destroy(conn);
	return NULL;
}

static int
sp_cbeconn_shutdown(Spconn *conn)
{
	int i;
	Spcbeconn *cbeconn;

	cbeconn = conn->caux;
	for(i = 0; i < 8; i++)
		if (cbeconn->reqs[i].state != Rfree)
			break;

	if (i < 8)
		return 0;

//	free(cbeconn);
	return 1;
}

static void
sp_cbeconn_dataout(Spconn *conn, Spreq *req)
{
	int n;
	Spcbeconn *cbeconn;
	Spcbereq *creq;

	if (conn->srv->debuglevel) {
		fprintf(stderr, ">>> (%p) ", conn);
		sp_printfcall(stderr, req->rcall, conn->dotu);
		fprintf(stderr, "\n");
	}

	cbeconn = conn->caux;
	creq = req->caux;
	creq->state = Rwdma;
	n = req->rcall->size;
	n += (n%128)?(128 - n%128):0;
	spe_mfcio_get(cbeconn->speid, creq->lsptr, req->rcall->pkt, n, creq->tag, 0, 0);
}

static void
sp_cbeconn_msg(Spconn *conn, u32 val)
{
	int i, lsptr, count;
	Spreq *req;
	Spcbeconn *cconn;
	Spcbereq *creq;

	cconn = conn->caux;
	lsptr = val & 0x3FFFF;
	count = val >> 18;
	count += (count%128)?(128 - count%128):0;
	req = sp_req_alloc(conn, NULL);
	for(i = 0; i < 8; i++)
		if (cconn->reqs[i].state == Rfree)
			break;

	assert(i < 8);
	creq = &cconn->reqs[i];
	req->caux = creq;
	creq->req = req;
	creq->state = Rrdma;
	creq->lsptr = lsptr;
	creq->tag = i;
	req->tcall = sp_cbeconn_fcall_alloc(conn);
	req->next = conn->ireqs;
	conn->ireqs = req;
	spe_mfcio_put(cconn->speid, lsptr, req->tcall->pkt, count, creq->tag, 0, 0);
}

static void
sp_cbeconn_dma(Spconn *conn, u32 mask)
{
	int i, size, dma;
	u32 sig;
	Spfcall *tc;
	Spreq *req, *pr, *r;
	Spcbeconn *cconn;
	Spcbereq *creq;

	cconn = conn->caux;
	dma = 0;
	sig = 0;
	for(i = 0; i < 8; i++) {
		creq = &cconn->reqs[i];

		if (!(mask & (1 << i))) {
			if (creq->state==Rrdma || creq->state==Rwdma)
				dma = 1;
			continue;
		}

		req = creq->req;
		if (creq->state==Rrdma && !(conn->flags&Creset)) {
			tc = req->tcall;
			size = tc->pkt[0] | (tc->pkt[1]<<8) | (tc->pkt[2]<<16) | (tc->pkt[3]<<24);
			if (size > conn->msize) {
				// TODO: close the connection
				fprintf(stderr, "error: packet too big\n");
				return;
			}

			if (!sp_deserialize(req->tcall, req->tcall->pkt, conn->dotu)) {
				fprintf(stderr, "error while deserializing\n");
				// TODO: close the connection
				return;
			}

			if (conn->srv->debuglevel) {
				fprintf(stderr, "<<< (%p) ", conn);
				sp_printfcall(stderr, req->tcall, conn->dotu);
				fprintf(stderr, "\n");
			}

			for(pr=NULL, r=conn->ireqs; r!=NULL && r!=req; pr=r, r=r->next)
				;

			assert(r != NULL);
			if (pr)
				pr->next = r->next;
			else
				conn->ireqs = r->next;

			req->tag = tc->tag;
			creq->state = Rwork;
			sp_srv_process_req(req);
		} else if (creq->state == Rwdma) {
			for(pr=NULL, r=conn->oreqs; r!=NULL && r!=req; pr=r, r=r->next)
				;

			assert(r != NULL);
			sig |= 1 << (req->tag==NOTAG?0:req->tag);
			if (pr)
				pr->next = r->next;
			else
				conn->oreqs = r->next;

			free(req->rcall);
			sp_cbeconn_fcall_free(conn, req->tcall);
			sp_req_free(req);
			creq->state = Rfree;
		} else
			abort();
	}

	if (sig)
		spe_signal_write(cconn->speid, SPE_SIG_NOTIFY_REG_2, sig);

	if (conn->flags&Creset && !dma) {
		sp_conn_reset(conn, conn->msize, conn->dotu);
		if (conn->flags & Cshutdown)
			sp_conn_destroy(conn);
	}
}

Spsrv*
sp_cbesrv_create(void)
{
	Spsrv *srv;
	Spcbesrv *csrv;

	csrv = sp_malloc(sizeof(*csrv));
	if (!csrv)
		return NULL;

	csrv->evhandler = spe_event_handler_create();
	if (!csrv->evhandler) {
		free(csrv);
		sp_uerror(errno);
		return NULL;
	}

	srv = sp_srv_create();
	if (!srv) {
		free(csrv);
		return NULL;
	}

	srv->srvaux = csrv;
	srv->start = sp_cbesrv_start;
	srv->shutdown = sp_cbesrv_shutdown;
	srv->destroy = sp_cbesrv_destroy;

	return srv;
}

static void
sp_cbesrv_start(Spsrv *srv)
{
}

static void
sp_cbesrv_shutdown(Spsrv *srv)
{
}

static void
sp_cbesrv_destroy(Spsrv *srv)
{
	free(srv->srvaux);
}

int
sp_cbesrv_loop(Spsrv *srv)
{
	int n;
	Spconn *conn;
	Spcbeconn *cconn;
	Spcbesrv *csrv;
	spe_event_unit_t ev;

	csrv = srv->srvaux;
	while (1) {
		ev.events = 0;
		n = spe_event_wait(csrv->evhandler, &ev, 1, 1000);
		if (!n)
			continue;

		cconn = ev.data.ptr;
		if (cconn->stopped)
			continue;

		conn = cconn->conn;
		if (ev.events & SPE_EVENT_OUT_INTR_MBOX) {
			n = 42;
			spe_out_intr_mbox_read(ev.spe, &n, 1, SPE_MBOX_ALL_BLOCKING);
			sp_cbeconn_msg(conn, n);
		}

		if (!conn) {
			sp_werror("unexpected spuid", EIO);
			return -1;
		}

		if (ev.events & SPE_EVENT_TAG_GROUP) {
			n = 42;
			spe_mfcio_tag_status_read(ev.spe, 0, SPE_TAG_ANY, &n);
			sp_cbeconn_dma(conn, n);
		}

		if (ev.events & SPE_EVENT_SPE_STOPPED) {
			/* It looks that we get this even more than once.
			   We are going to free conn, but make sure cconn
			   stays around for the cconn->stopped check */
			cconn->stopped++;
			sp_conn_shutdown(conn);
		}
	}

	return 0;
}

static Spfcall *
sp_cbeconn_fcall_alloc(Spconn *conn)
{
	unsigned long n;
	Spfcall *ret;

	n = conn->msize;
	n += (n%128)?(128-n%128):0;
	ret = sp_malloc(sizeof(*ret) + 128 + n);
	if (!ret)
		return NULL;

	n = (unsigned long)((u8 *)ret + sizeof(*ret));
	n += (n%128)?(128-n%128):0;
	ret->pkt = (u8 *) n;

	return ret;
}

static void
sp_cbeconn_fcall_free(Spconn *conn, Spfcall *fc)
{
	free(fc);
}
