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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>

#include "npfs.h"
#include "npclient.h"
#include "copyfile.h"
#include "rx.h"

int
rxfile_read(Rxfile *f, char *buf, int buflen)
{
	return f->read(f, buf, buflen);
}

int 
rxfile_write(Rxfile *f, char *buf, int buflen)
{
	return f->write(f, buf, buflen);
}

static int
rxfile_uread(Rxfile *f, char *buf, int buflen)
{
	int n;

	n = read(f->fd, buf, buflen);
	if (n < 0)
		np_uerror(errno);

	return n;
}

static int
rxfile_uwrite(Rxfile *f, char *buf, int buflen)
{
	int n;

	n = write(f->fd, buf, buflen);
	if (n < 0)
		np_uerror(errno);

	return n;
}

static int
rxfile_9read(Rxfile *f, char *buf, int buflen)
{
	int n;

	n = npc_read(f->fid, (u8 *) buf, buflen, f->off);
	if (n > 0)
		f->off += n;

	return n;
}

static int
rxfile_9write(Rxfile *f, char *buf, int buflen)
{
	int n;

	n = npc_write(f->fid, (u8 *) buf, buflen, f->off);
	if (n > 0)
		f->off += n;

	return n;
}

Rxfile *
rxfile_open(char *path, int mode)
{
	Rxfile *f;

	f = malloc(sizeof(*f));
	if (!f) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	f->fid = NULL;
	f->off = 0;
	f->fd = open(path, mode);
	if (f->fd < 0) {
		np_uerror(errno);
		free(f);
		return NULL;
	}

	f->read = &rxfile_uread;
	f->write = &rxfile_uwrite;

	return f;
}

Rxfile *
rxfile_9open(Npcfsys *fsys, char *path, int mode)
{
	u32 perm;
	Rxfile *f;

	f = malloc(sizeof(*f));
	if (!f) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	perm = Oread;
	if (mode & O_RDONLY)
		perm = Oread;
	else if (mode & O_WRONLY)
		perm = Owrite;
	else if (mode & O_RDWR)
		perm = Ordwr;

	f->fd = -1;
	f->off = 0;
	f->fid = npc_open(fsys, path, perm);
	if (!f->fid) {
		free(f);
		return NULL;
	}

	f->read = &rxfile_9read;
	f->write = &rxfile_9write;

	return f;
}

void
rxfile_close(Rxfile *f)
{
	if (f->fd > 0)
		close(f->fd);

	if (f->fid)
		npc_close(f->fid);
}
