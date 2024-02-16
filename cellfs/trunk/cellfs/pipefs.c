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
#include <fcntl.h>
#include <time.h>
#include <assert.h>
#include <sys/mman.h>
#include "spfs.h"
#include "yspufs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct Buf Buf;
typedef struct Pipe Pipe;
typedef struct Req Req;

struct Buf {
	int		size;
	int		pos;
	u8*		data;
	Buf*		next;
};

struct Pipe {
	int		size;
	Buf*		bufs;

	Req*		wreqs;
	Req*		rreqs;
};

struct Req {
	Spreq*		req;
	Req*		next;
};

static Spfile* pipefs_create(Spfile *dir, char *name, u32 perm, Spuser *uid,
	Spgroup *gid, char *extension);
static int pipefs_read(Spfilefid* file, u64 offset, u32 count, u8* data, Spreq *);
static int pipefs_write(Spfilefid* file, u64 offset, u32 count, u8* data, Spreq *);
static int pipefs_wstat(Spfile*, Spstat*);
static Spfile* dir_first(Spfile *dir);
static Spfile* dir_next(Spfile *dir, Spfile *prevchild);

static Spfile *create_file(Spfile *parent, char *name, u32 mode, u64 qpath, 
	void *ops, Spuser *usr, void *aux);
static Spfile *dir_first(Spfile *dir);
static Spfile *dir_next(Spfile *dir, Spfile *prevchild);

static Spdirops root_ops = {
	.create = pipefs_create,
	.first = dir_first,
	.next = dir_next,
};

static Spfileops pipe_ops = {
	.read = pipefs_read,
	.write = pipefs_write,
	.wstat = pipefs_wstat,
};

static Spuser *user;
static Spfile *root;
static int qidpath = 100;

int
pipefs_init()
{
	Spsrv *srv;

	user = sp_uid2user(geteuid());
	root = spfile_alloc(NULL, strdup(""), ROOTPERM|Dmdir, 0, &root_ops, NULL);
	root->parent = root;
	root->atime = time(NULL);
	root->mtime = root->atime;
	root->uid = user;
	root->gid = user->dfltgroup;
	root->muid = user;
	spfile_incref(root);

	srv = sp_srv_create();
	if (!srv)
		return -1;

	spfile_init_srv(srv, root);
	register_dev('p', srv);

	return 0;
}

static Spfile *
create_file(Spfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Spuser *usr, void *aux)
{
	Spfile *ret;

	ret = spfile_alloc(parent, name, mode, qpath, ops, aux);
	if (!ret)
		return NULL;

	if (parent) {
		if (parent->dirlast) {
			parent->dirlast->next = ret;
			ret->prev = parent->dirlast;
		} else
			parent->dirfirst = ret;

		parent->dirlast = ret;
		if (!usr)
			usr = parent->uid;
	}

	if (!usr)
		usr = user;

	ret->atime = ret->mtime = time(NULL);
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
	spfile_incref(ret);
	return ret;
}

static Spfile*
dir_first(Spfile *dir)
{
	spfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Spfile*
dir_next(Spfile *dir, Spfile *prevchild)
{
	spfile_incref(prevchild->next);
	return prevchild->next;
}

static Spfile *
pipefs_create(Spfile *dir, char *name, u32 perm, Spuser *uid, Spgroup *gid, char *extension)
{
	Pipe *p;

	p = sp_malloc(sizeof(*p));
	if (!p)
		return NULL;

	p->size = 0;
	p->bufs = NULL;
	p->wreqs = NULL;
	p->rreqs = NULL;

	return create_file(dir, name, perm, qidpath++, &pipe_ops, NULL, p);
}

static void
buf_add(Pipe *p, u8 *data, u32 count)
{
	Buf *buf, *pb, *b;

	buf = sp_malloc(sizeof(*buf) + count);
	buf->size = count;
	buf->pos = 0;
	buf->data = (u8 *) buf + sizeof(*buf);
	buf->next = NULL;
	memmove(buf->data, data, count);

	for(pb = NULL, b = p->bufs; b != NULL; pb = b, b = b->next)
		;

	if (pb)
		pb->next = buf;
	else
		p->bufs = buf;

	p->size += count;
}

static void
pipe_do(Pipe *p)
{
	int n;
	Buf *buf;
	Req *r;
	Spfcall *rc;

	r = p->rreqs;
	if (!r)
		return;

	buf = p->bufs;
	if (!buf)
		return;

	n = buf->size - buf->pos;
	if (n > r->req->tcall->count)
		n = r->req->tcall->count;

	rc = sp_create_rread(n, buf->data + buf->pos);
	sp_respond(r->req, rc);
	buf->pos += n;
	p->rreqs = r->next;
	free(r);

	if (buf->pos >= buf->size) {
		p->bufs = buf->next;
		p->size -= buf->size;
		free(buf);
	}

	while (p->size<32768 && p->wreqs) {
		r = p->wreqs;
		buf_add(p, r->req->tcall->data, r->req->tcall->count);
		rc = sp_create_rwrite(r->req->tcall->count);
		sp_respond(r->req, rc);
		p->wreqs = r->next;
		free(r);
	}
}

static int 
pipefs_read(Spfilefid *fid, u64 offset, u32 count, u8* data, Spreq *req)
{
	Pipe *p;
	Req *pr, *r;

	p = fid->file->aux;
	for(pr=NULL, r = p->rreqs; r != NULL; pr = r, r = r->next)
		;

	r = sp_malloc(sizeof(*r));
	r->req = req;
	r->next = NULL;

	if (pr)
		pr->next = r;
	else
		p->rreqs = r;

	pipe_do(p);
	return -1;
}

static int 
pipefs_write(Spfilefid *fid, u64 offset, u32 count, u8* data, Spreq *req)
{
	Pipe *p;
	Req *pr, *r;

	p = fid->file->aux;

	/* if there is too much data in the buffers, pospone responding to the request */
	if (p->size > 32768) {
		for(pr=NULL, r = p->wreqs; r != NULL; pr = r, r = r->next)
			;

		r = sp_malloc(sizeof(*r));
		r->req = req;
		r->next = NULL;

		if (pr)
			pr->next = r;
		else
			p->wreqs = r;

		return -1;
	}

	buf_add(p, data, count);
	pipe_do(p);

	return count;
}

static int 
pipefs_wstat(Spfile *file, Spstat *st)
{
	return 1;
}
