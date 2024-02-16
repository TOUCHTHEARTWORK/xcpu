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

typedef struct Logfd Logfd;
struct Logfd {
	FILE*	f;
	char	fmt[256];
	char*	cs;
};

static int log_write(Spfilefid* file, u64 offset, u32 count, u8* data, Spreq *);
static int log_wstat(Spfile*, Spstat*);
static Spfile* dir_first(Spfile *dir);
static Spfile* dir_next(Spfile *dir, Spfile *prevchild);

static Spfile *create_file(Spfile *parent, char *name, u32 mode, u64 qpath, 
	void *ops, Spuser *usr, void *aux);
static Spfile *dir_first(Spfile *dir);
static Spfile *dir_next(Spfile *dir, Spfile *prevchild);

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Spfileops log_ops = {
	.write = log_write,
	.wstat = log_wstat,
};

static Spuser *user;
static Spfile *root;
static Logfd logout;
static Logfd logerr;

int
logfs_init()
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

	logout.f = stdout;
	logout.fmt[0] = '\0';
	logout.cs = NULL;

	logerr.f = stdout;
	logerr.fmt[0] = '\0';
	logerr.cs = NULL;

	if (create_file(root, "stdout", 0666, 1, &log_ops, NULL, &logout) < 0)
		goto error;

	if (create_file(root, "stderr", 0666, 2, &log_ops, NULL, &logerr) < 0)
		goto error;

	srv = sp_srv_create();
	if (!srv)
		return -1;

	spfile_init_srv(srv, root);
	register_dev('l', srv);
	srv->debuglevel = 0;

	return 0;

error:
	return -1;
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

static int 
log_write(Spfilefid *fid, u64 offset, u32 count, u8* data, Spreq *req)
{
	int n;
	char *fmt, *s;
	Logfd *l;
	char *fp;
	u8 *dp, *de;

	if (count == 0) {
		fprintf(l->f, "format string too long\n");
		return count;
	}

	l = fid->file->aux;
	fmt = (char *) data;
	fp = fmt;
	dp = data + strlen(fmt) + 1;
	de = data + count;

	while (*fp != '\0') {
		s = strchr(fp, '%');
		if (!s)
			s = fp + strlen(fp);

		fprintf(l->f, "%.*s", (int) (s-fp), fp);
		fp = s;
		if (*fp == '\0')
			break;

		fp++;
		switch (*fp) {
		case 'l':
			fp++;
			if (dp+sizeof(long long int) <= de) {
				if (*fp == 'x')
					fprintf(l->f, "%llx", *(long long int *) dp);
				else
					fprintf(l->f, "%lld", *(long long int *) dp);
			}
			dp += sizeof(long long int);
			break;

		case 'd':
			if (dp+sizeof(int) <= de)
				fprintf(l->f, "%d", *(int *) dp);
			dp += sizeof(int);
			break;

		case 'x':
		case 'p':
			if (dp+sizeof(int) <= de)
				fprintf(l->f, "%x", *(int *) dp);
			dp += sizeof(int);
			break;

		case 'f':
			if (dp+sizeof(double) <= de)
				fprintf(l->f, "%f", *(double *) dp);
			dp += sizeof(double);
			break;

		case 's':
			n = strlen((char *) dp);
			if (dp+n <= de)
				fprintf(l->f, "%s", (char *) dp);
			dp += n;
			break;

		default:
			fprintf(l->f, "%%%c", *dp);
			break;
		}

		fp++;
	}

	fflush(l->f);
	return count;
}

static int 
log_wstat(Spfile *file, Spstat *st)
{
	return 1;
}
