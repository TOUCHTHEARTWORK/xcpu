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
#include <assert.h>
#include <sys/mman.h>
#include <time.h>
#include "spfs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct Dev Dev;

struct Dev {
	Spsrv*		srv;
//	Spfid*		root;
//	Spqid		rqid;
};

static int nconns;
static Spuser *user;
static Spsrv *rootsrv;
static Dev devs[256];

static Spfcall* rootfs_attach(Spfid *fid, Spfid *afid, Spstr *uname, Spstr *aname);
static int rootfs_clone(Spfid *fid, Spfid *newfid);
static int rootfs_walk(Spfid *fid, Spstr* wname, Spqid *wqid);
static Spfcall* rootfs_open(Spfid *fid, u8 mode);
static Spfcall* rootfs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, 
	Spstr *extension);
static Spfcall* rootfs_read(Spfid *fid, u64 offset, u32 count, Spreq *);
static Spfcall* rootfs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *);
static Spfcall* rootfs_clunk(Spfid *fid);
static Spfcall* rootfs_remove(Spfid *fid);
static Spfcall* rootfs_stat(Spfid *fid);
static Spfcall* rootfs_wstat(Spfid *fid, Spstat *stat);
static void rootfs_fiddestroy(Spfid *fid);
static void rootfs_connopen(Spconn *conn);
static void rootfs_connclose(Spconn *conn);

Spsrv *
rootfs_getsrv(Spfid *fid)
{
	Spsrv *srv;

	srv = devs[fid->dev].srv;
	if (!srv)
		sp_werror("invalid device", EIO);

	fid->conn->srv = srv;
	return srv;
}

void
rootfs_putsrv(Spfid *fid)
{
	fid->conn->srv = rootsrv;
}

static void
rootfs_connopen(Spconn *conn)
{
	nconns++;
}

static void
rootfs_connclose(Spconn *conn)
{
	nconns--;
	if (nconns==0)
		exit(0);
}

static void
rootfs_fiddestroy(Spfid *fid)
{
	Spsrv *srv;

	if (!fid->dev)
		return;

	srv = rootfs_getsrv(fid);
	if (!srv)
		return;

	(*srv->fiddestroy)(fid);
	rootfs_putsrv(fid);
}

static Spfcall*
rootfs_attach(Spfid *nfid, Spfid *nafid, Spstr *uname, Spstr *aname)
{
	char *u;
	Spfcall* ret;
	Spuser *user;
	Spqid qid;

	user = NULL;
	ret = NULL;

	if (nafid != NULL) {
		sp_werror(Enoauth, EIO);
		return NULL;
	}

	u = sp_strdup(uname);
	user = sp_uid2user(geteuid());
	free(u);
	if (!user) {
		sp_werror(Eunknownuser, EIO);
		return NULL;
	}

	nfid->user = user;
	nfid->dev = 0;
	sp_fid_incref(nfid);

	qid.type = 0;
	qid.version = 0;
	qid.path = 0;

	return sp_create_rattach(&qid);
}

static int
rootfs_clone(Spfid *fid, Spfid *newfid)
{
	int n;
	Spsrv *srv;

	newfid->dev = fid->dev;
	if (!fid->dev)
		return 1;

	srv = rootfs_getsrv(fid);
	if (!srv)
		return 0;

	n = (*srv->clone)(fid, newfid);
	rootfs_putsrv(fid);
	return n;
}

static int
rootfs_walk(Spfid *fid, Spstr* wname, Spqid *wqid)
{
	int n;
	Spsrv *srv;
	Spfcall *fc;
	Spstr uname;
	Spstr aname;

	if (!fid->dev) {
		if (wname->len!=2 || wname->str[0]!='#' || !devs[(int) wname->str[1]].srv) {
			sp_werror(Enotfound, ENOENT);
			return 0;
		}

		fid->dev = wname->str[1];
		fid->refcount = 0;
		srv = rootfs_getsrv(fid);
//		srv = devs[fid->dev].srv;
		uname.len = strlen(user->uname);
		uname.str = user->uname;
		aname.len = 0;
		aname.str = "";
		fc = (*srv->attach)(fid, NULL, &uname, &aname);
		rootfs_putsrv(fid);
		if (!fc)
			return 0;

		*wqid = fc->qid;
		free(fc);
		return 1;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return 0;

	n = (*srv->walk)(fid, wname, wqid);
	rootfs_putsrv(fid);
	return n;
}

static Spfcall*
rootfs_open(Spfid *fid, u8 mode)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->open)(fid, mode);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->create)(fid, name, perm, mode, extension);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_read(Spfid *fid, u64 offset, u32 count, Spreq *req)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->read)(fid, offset, count, req);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->write)(fid, offset, count, data, req);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_clunk(Spfid *fid)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		return sp_create_rclunk();
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->clunk)(fid);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_remove(Spfid *fid)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->remove)(fid);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_stat(Spfid *fid)
{
	Spsrv *srv;
	Spwstat wstat;
	Spfcall *fc;

	if (!fid->dev) {
		wstat.type = 0;
		wstat.dev = 0;
		wstat.qid.type = 0;
		wstat.qid.version = 0;
		wstat.qid.path = 0;
		wstat.mode = 0666;
		wstat.atime = time(NULL);
		wstat.mtime = time(NULL);
		wstat.length = 0;
		wstat.name = "";
		wstat.uid = "";
		wstat.gid = "";
		wstat.muid = "";
		return sp_create_rstat(&wstat, fid->conn->dotu);
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->stat)(fid);
	rootfs_putsrv(fid);
	return fc;
}

static Spfcall*
rootfs_wstat(Spfid *fid, Spstat *stat)
{
	Spsrv *srv;
	Spfcall *fc;

	if (!fid->dev) {
		sp_werror(Eperm, EPERM);
		return NULL;
	}

	srv = rootfs_getsrv(fid);
	if (!srv)
		return NULL;

	fc = (*srv->wstat)(fid, stat);
	rootfs_putsrv(fid);
	return fc;
}

int
register_dev(int dev, Spsrv *srv)
{
	if (dev<=0 || dev>256) {
		sp_werror("invalid dev number", EIO);
		return -1;
	}

	if (devs[dev].srv) {
		sp_werror("device already registered", EIO);
		return -1;
	}

	srv->debuglevel = rootsrv->debuglevel;
	devs[dev].srv = srv;
//	devs[dev].root = NULL;

	return 0;
}

Spsrv *
rootfs_init(int debuglevel)
{
	rootsrv = sp_cbesrv_create();
	if (!rootsrv)
		return NULL;

	rootsrv->dotu = 0;
	rootsrv->connopen = rootfs_connopen;
	rootsrv->connclose = rootfs_connclose;
	rootsrv->attach = rootfs_attach;
	rootsrv->clone = rootfs_clone;
	rootsrv->walk = rootfs_walk;
	rootsrv->open = rootfs_open;
	rootsrv->create = rootfs_create;
	rootsrv->read = rootfs_read;
	rootsrv->write = rootfs_write;
	rootsrv->clunk = rootfs_clunk;
	rootsrv->remove = rootfs_remove;
	rootsrv->stat = rootfs_stat;
	rootsrv->wstat = rootfs_wstat;
	rootsrv->fiddestroy = rootfs_fiddestroy;
	rootsrv->debuglevel = debuglevel;

	user = sp_uid2user(geteuid());
	devs[0].srv = rootsrv;
	return rootsrv;
}

void
rootfs_loop(void)
{
	sp_cbesrv_loop(rootsrv);
}
