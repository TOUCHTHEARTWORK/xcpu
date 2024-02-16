/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

extern int printfcall(FILE *f, Npfcall *fc, int dotu);

static Npfcall *np_conn_new_incall(Npconn *conn);
static void np_conn_free_incall(Npconn *, Npfcall *);
static void *np_conn_read_proc(void *);

Npconn*
np_conn_create(Npsrv *srv, Nptrans *trans)
{
	Npconn *conn;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return NULL;

	//fprintf(stderr, "np_conn_create %p\n", conn);
	pthread_mutex_init(&conn->lock, NULL);
	pthread_cond_init(&conn->resetcond, NULL);
	pthread_cond_init(&conn->resetdonecond, NULL);
	conn->resetting = 0;
	conn->srv = srv;
	conn->msize = srv->msize;
	conn->dotu = srv->dotu;
	conn->shutdown = 0;
	conn->fidpool = np_fidpool_create();
	conn->trans = trans;
	conn->aux = NULL;
	conn->freercnum = 0;
	conn->freerclist = NULL;

	pthread_create(&conn->rthread, NULL, np_conn_read_proc, conn);
	return conn;
}

static void *
np_conn_read_proc(void *a)
{
	int i, n, size, msize;
	Npsrv *srv;
	Npconn *conn;
	Nptrans *trans;
	Npreq *req;
	Npfcall *fc, *fc1;

	pthread_detach(pthread_self());
	conn = a;
	srv = conn->srv;
	msize = conn->msize;
	fc = np_conn_new_incall(conn);
	n = 0;
	while ((i = np_trans_read(conn->trans, fc->pkt + n, msize - n)) > 0) {
		pthread_mutex_lock(&conn->lock);
		if (conn->resetting) {
			pthread_cond_wait(&conn->resetdonecond, &conn->lock);
			n = 0;	/* discard all input */
			i = 0;
		}
		pthread_mutex_unlock(&conn->lock);
		n += i;

again:
		if (n < 4)
			continue;

		size = fc->pkt[0] | (fc->pkt[1]<<8) | (fc->pkt[2]<<16) | (fc->pkt[3]<<24);
		if (n < size)
			continue;

		if (!np_deserialize(fc, fc->pkt, conn->dotu))
			break;

		if (conn->srv->debuglevel) {
			fprintf(stderr, "<<< (%p) ", req);
			np_printfcall(stderr, fc, conn->dotu);
			fprintf(stderr, "\n");
		}

		fc1 = np_conn_new_incall(conn);
		if (n > size)
			memmove(fc1->pkt, fc->pkt + size, n - size);
		n -= size;

		req = reqalloc();
		req->conn = conn;
		req->tcall = fc;
		req->tag = req->tcall->tag;
		pthread_mutex_lock(&conn->srv->lock);
		req->prev = srv->reqs_last;
		if (srv->reqs_last)
			srv->reqs_last->next = req;
		srv->reqs_last = req;
		if (!srv->reqs_first)
			srv->reqs_first = req;
		pthread_mutex_unlock(&conn->srv->lock);
		pthread_cond_signal(&conn->srv->reqcond);
		fc = fc1;
		if (n > 0)
			goto again;

	}

	pthread_mutex_lock(&conn->lock);
	trans = conn->trans;
	conn->trans = NULL;
	np_conn_free_incall(conn, fc);
	pthread_mutex_unlock(&conn->lock);

	np_srv_remove_conn(conn->srv, conn);
	np_conn_reset(conn, 0, 0);

	if (trans)
		np_trans_destroy(trans);

	return NULL;
}



void
np_conn_reset(Npconn *conn, u32 msize, int dotu)
{
	int i, n;
	Npsrv *srv;
	Npreq *req, *req1, **prevp, **reqs;
	Npfcall *fc, *fc1;

	pthread_mutex_lock(&conn->lock);
	conn->resetting = 1;
	pthread_mutex_unlock(&conn->lock);
	
	pthread_mutex_lock(&conn->srv->lock);
	srv = conn->srv;
	// first flush all outstanding requests
	req = srv->reqs_first;
	while (req != NULL) {
		if (req->conn == conn) {
			req1 = req->next;
			np_srv_remove_request(srv, req);
			np_conn_respond(req);
			req = req1;
		} else {
			prevp = &req->next;
			req = *prevp;
		}
	}

	// then flush all working requests
	n = 0;
	req = conn->srv->workreqs;
	while (req != NULL) {
		if (req->conn == conn && req->tcall->type != Tversion) {
			n++;
			pthread_mutex_lock(&req->lock);
			req->cancelled = 1;
			pthread_mutex_unlock(&req->lock);
		}

		req = req->next;
	}

	reqs = malloc(n * sizeof(Npreq *));
	n = 0;
	req = conn->srv->workreqs;
	while (req != NULL) {
		if (req->conn == conn && (msize==0 || req->tcall->type != Tversion))
			reqs[n++] = req;
		req = req->next;
	}
	pthread_mutex_unlock(&conn->srv->lock);

	for(i = 0; i < n; i++) {
		/* if the request is marked as cancelled, it is safe to access it, 
		   nobody else will free it */
		req = reqs[i];
		if (!req->responded && req->conn->srv->flush)
			(*req->conn->srv->flush)(req);
	}

	/* wait until all working requests finish */
	pthread_mutex_lock(&conn->lock);
	while (1) {
		for(i = 0; i < n; i++) 
			if (!reqs[i]->cancelled)
				break;

		if (i >= n)
			break;

		pthread_cond_wait(&conn->resetcond, &conn->lock);
	}

	/* then free them */
	for(i = 0; i < n; i++) {
		req = reqs[i];
		np_conn_free_incall(req->conn, req->tcall);
		free(req->rcall);
		reqfree(req);
	}
	free(reqs);

	/* free old pool of fcalls */	
	fc = conn->freerclist;
	conn->freerclist = NULL;
	while (fc != NULL) {
		fc1 = fc->next;
		free(fc);
		fc = fc1;
	}

	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}

	if (msize) {
		conn->dotu = dotu;
		conn->resetting = 0;
		conn->fidpool = np_fidpool_create();
		pthread_mutex_unlock(&conn->lock);
		pthread_cond_broadcast(&conn->resetdonecond);
	} else {
		pthread_mutex_unlock(&conn->lock);
		pthread_mutex_destroy(&conn->lock);
		free(conn);
	}
}

void
np_conn_shutdown(Npconn *conn)
{
	Nptrans *trans;

	pthread_mutex_lock(&conn->lock);
	trans = conn->trans;
	conn->trans = NULL;
	pthread_mutex_unlock(&conn->lock);

	np_trans_destroy(trans);
}

void
np_conn_respond(Npreq *req)
{
	int n;
	Npconn *conn;
	Nptrans *trans;
	Npfcall *rc;

	conn = req->conn;
	rc = req->rcall;

	if (req->cancelled || !rc || conn->resetting || !conn->trans) {
		req->cancelled = 0;
		pthread_cond_broadcast(&conn->resetcond);
		return;
	}

	pthread_mutex_lock(&conn->lock);
	if (conn->srv->debuglevel) {
		fprintf(stderr, ">>> (%p) ", req);
		np_printfcall(stderr, rc, conn->dotu);
		fprintf(stderr, "\n");
//		dump(stderr, rc->pkt, rc->size);
	}
	n = np_trans_write(conn->trans, rc->pkt, rc->size);
	if (n <= 0) {
		trans = conn->trans;
		conn->trans = NULL;
	}
	np_conn_free_incall(req->conn, req->tcall);
	free(req->rcall);
	pthread_mutex_unlock(&conn->lock);

	if (n <= 0)
		np_trans_destroy(trans); /* np_conn_read_proc will take care of resetting */

}

static Npfcall *
np_conn_new_incall(Npconn *conn)
{
	Npfcall *fc;

	pthread_mutex_lock(&conn->lock);
//	if (!conn->trans) {
//		pthread_mutex_unlock(&conn->lock);
//		return NULL;
//	}

	if (conn->freerclist) {
		fc = conn->freerclist;
		conn->freerclist = fc->next;
		conn->freercnum--;
	} else {
		fc = malloc(sizeof(*fc) + conn->msize);
	}

	if (!fc) {
		pthread_mutex_unlock(&conn->lock);
		return NULL;
	}

	fc->pkt = (u8*) fc + sizeof(*fc);
	pthread_mutex_unlock(&conn->lock);

	return fc;
}

static void
np_conn_free_incall(Npconn* conn, Npfcall *rc)
{
//	pthread_mutex_lock(&conn->lock);
	if (conn->freercnum < 64) {
		rc->next = conn->freerclist;
		conn->freerclist = rc;
		rc = NULL;
	}
//	pthread_mutex_unlock(&conn->lock);

	if (rc)
		free(rc);
}
