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
#include "xcpu.h"
#include "rx.h"

static Rxpipe *rxpipe_create(Rxsession *rx, Rxfile *file, int fd);
static void *rxsession_waitproc(void *a);

Rxsession *
rxsession_create(char *mntpt, int stdoutfd, int stderrfd, int stdinfd, pthread_mutex_t *lock, pthread_cond_t *cond)
{
	int n, buflen, cfd;
	char *buf;
	Rxsession *rx;

	buf = NULL;
	cfd = -1;
	rx = malloc(sizeof(*rx));
	if (!rx) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}
	memset(rx, 0, sizeof(*rx));

	rx->lock = lock;
	rx->cond = cond;
	rx->finished = 0;
	rx->mntpt = strdup(mntpt);
	if (!rx->mntpt) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}

	rx->addr = rxmntpt2addr(mntpt);
	if (rx->addr)
		rx->addr = strdup(rx->addr);

	if (!rx->addr) {
		np_werror("cannot find xcpufs address", EIO);
		goto error;
	}
		
	buflen = strlen(mntpt) + 64;
	buf = malloc(buflen);
	if (!buf) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}

	snprintf(buf, buflen, "%s/arch", mntpt);
	cfd = open(buf, O_RDONLY);
	if (cfd < 0) {
		np_uerror(errno);
		goto error;
	}

	if ((n = read(cfd, buf, buflen)) < 0) {
		np_uerror(errno);
		goto error;
	}

	buf[n] = '\0';
	rx->arch = strdup(buf);
	if (!rx->arch) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}
	close(cfd);

	snprintf(buf, buflen, "%s/clone", mntpt);
	cfd = open(buf, O_RDONLY);
	if (cfd < 0) {
		np_uerror(errno);
		goto error;
	}

	if ((n = read(cfd, buf, buflen)) < 0) {
		np_uerror(errno);
		goto error;
	}

	buf[n] = '\0';
	rx->sid = strdup(buf);
	if (!rx->sid) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}
	
	snprintf(buf, buflen, "%s/%s/ctl", mntpt, rx->sid);
	rx->ctl = rxfile_open(buf, O_RDWR);
	if (!rx->ctl)
		goto error;

	snprintf(buf, buflen, "%s/%s/wait", mntpt, rx->sid);
	rx->wait = rxfile_open(buf, O_RDONLY);
	if (!rx->wait)
		goto error;
	pthread_create(&rx->waitproc, NULL, rxsession_waitproc, rx);

	if (stdinfd >= 0) {
		snprintf(buf, buflen, "%s/%s/stdin", mntpt, rx->sid);
		rx->stin = rxfile_open(buf, O_WRONLY);
		if (!rx->stin)
			goto error;
	}

	if (stdoutfd >= 0) {
		snprintf(buf, buflen, "%s/%s/stdout", mntpt, rx->sid);
		rx->stout = rxfile_open(buf, O_RDONLY);
		if (!rx->stout)
			goto error;

		rx->outpipe = rxpipe_create(rx, rx->stout, stdoutfd);
	}

	if (stderrfd >= 0) {
		snprintf(buf, buflen, "%s/%s/stderr", mntpt, rx->sid);
		rx->sterr = rxfile_open(buf, O_RDONLY);
		if (!rx->sterr)
			goto error;


		rx->errpipe = rxpipe_create(rx, rx->sterr, stderrfd);
	}

	/* TODO: error handling for rxpipe_create */

	close(cfd);
	return rx;

error:
	if (rx->sterr)
		rxfile_close(rx->sterr);

	if (rx->stout)
		rxfile_close(rx->stout);

	if (rx->stin)
		rxfile_close(rx->stin);

	if (rx->ctl)
		rxfile_close(rx->ctl);

	free(rx->sid);
	free(rx->addr);
	free(rx->mntpt);
	free(buf);
	free(rx);

	if (cfd >= 0)
		close(cfd);

	return NULL;
}

Rxsession *
rxsession_9create(char *addr, int stdoutfd, int stderrfd, int stdinfd, pthread_mutex_t *lock, pthread_cond_t *cond)
{
	int n;
	char buf[64];
	Npcfid *cfid;
	Rxsession *rx;
	Npuser *user;

	cfid = NULL;
	rx = malloc(sizeof(*rx));
	if (!rx) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}
	memset(rx, 0, sizeof(*rx));

	rx->lock = lock;
	rx->cond = cond;
	rx->finished = 0;
	rx->mntpt = strdup("");
	rx->addr = strdup(addr);
	if (!rx->addr) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}

	user = np_uid2user(getuid());
	if (!user) {
		np_werror("cannot find user name", EIO);
		goto error;
	}

	rx->fsys = npc_netmount(addr, user->uname, XCPU_PORT);
	if (!rx->fsys) 
		goto error;

	cfid = npc_open(rx->fsys, "arch", Oread);
	if (!cfid)
		goto error;

	if ((n = npc_read(cfid, (u8 *) buf, sizeof(buf), 0)) <= 0)
		goto error;

	buf[n] = '\0';
	rx->arch = strdup(buf);
	if (!rx->arch) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}
	npc_close(cfid);

	cfid = npc_open(rx->fsys, "clone", Oread);
	if (!cfid)
		goto error;

	if ((n = npc_read(cfid, (u8 *) buf, sizeof(buf), 0)) <= 0)
		goto error;

	buf[n] = '\0';
	rx->sid = strdup(buf);
	if (!rx->sid) {
		np_werror(Enomem, ENOMEM);
		goto error;
	}

	snprintf(buf, sizeof(buf), "%s/ctl", rx->sid);
	rx->ctl = rxfile_9open(rx->fsys, buf, Ordwr);
	if (!rx->ctl)
		goto error;

	snprintf(buf, sizeof(buf), "%s/wait", rx->sid);
	rx->wait = rxfile_9open(rx->fsys, buf, Oread);
	if (!rx->wait)
		goto error;

	pthread_create(&rx->waitproc, NULL, rxsession_waitproc, rx);

	if (stdinfd >= 0) {
		snprintf(buf, sizeof(buf), "%s/stdin", rx->sid);
		rx->stin = rxfile_9open(rx->fsys, buf, Owrite);
		if (!rx->stin)
			goto error;
	}

	if (stdoutfd >= 0) {
		snprintf(buf, sizeof(buf), "%s/stdout", rx->sid);
		rx->stout = rxfile_9open(rx->fsys, buf, Oread);
		if (!rx->stout)
			goto error;

		rx->outpipe = rxpipe_create(rx, rx->stout, 1);
	}

	if (stderrfd >= 0) {
		snprintf(buf, sizeof(buf), "%s/stderr", rx->sid);
		rx->sterr = rxfile_9open(rx->fsys, buf, Oread);
		if (!rx->sterr)
			goto error;

		rx->errpipe = rxpipe_create(rx, rx->sterr, 2);
	}

	/* TODO: error handling for rxpipe_create */

	npc_close(cfid);
	return rx;

error:
	if (rx->sterr)
		rxfile_close(rx->sterr);

	if (rx->stout)
		rxfile_close(rx->stout);

	if (rx->stin)
		rxfile_close(rx->stin);

	if (rx->ctl)
		rxfile_close(rx->ctl);

	if (cfid)
		npc_close(cfid);

	if (rx->fsys)
		npc_umount(rx->fsys);

	free(rx->sid);
	free(rx->addr);
	free(rx->mntpt);
	free(rx);

	return NULL;
}

int
rxsession_wipe(Rxsession *rx)
{
	if (rx->ctl)
		return rxfile_write(rx->ctl, "wipe\n", 5);

	return 0;
}

void
rxsession_destroy(Rxsession *rx)
{
	if (rx->sterr)
		rxfile_close(rx->sterr);

	if (rx->stout)
		rxfile_close(rx->stout);

	if (rx->stin)
		rxfile_close(rx->stin);

	if (rx->ctl)
		rxfile_close(rx->ctl);

	if (rx->fsys)
		npc_umount(rx->fsys);

	free(rx->sid);
	free(rx->addr);
	free(rx->mntpt);
	free(rx);
}

static void *
rxsession_waitproc(void *a)
{
	int n;
	Rxsession *rx;
	char buf[128];
	char *ename;
	int ecode;

	rx = a;
	n = rxfile_read(rx->wait, buf, sizeof(buf));
	pthread_mutex_lock(rx->lock);
	if (n <= 0) {
		np_rerror(&ename, &ecode);
		fprintf(stderr, "waitproc error: %d %s\n", ecode, ename);
		rx->exitcode = strdup("unknown exit code");
	} else {
		buf[n] = '\0';
		rx->exitcode = strdup(buf);
	}

	rx->finished = 1;
	pthread_cond_broadcast(rx->cond);
	pthread_mutex_unlock(rx->lock);

	return NULL;
}

static void *
rxsession_pipe_proc(void *a)
{
	int n, buflen;
	Rxpipe *p;
	char *buf;

	p = a;
	buflen = 8192;
	buf = malloc(buflen);
	if (!buf) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	while ((n = rxfile_read(p->file, buf, buflen)) > 0)
		write(p->fd, buf, n);

done:
	free(buf);
	np_rerror(&p->ename, &p->ecode);
	return NULL;
}

static Rxpipe *
rxpipe_create(Rxsession *rx, Rxfile *file, int fd)
{
	int n;
	Rxpipe *p;

	p = malloc(sizeof(*p));
	if (!p) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	p->rx = rx;
	p->file = file;
	p->fd = fd;
	n = pthread_create(&p->thread, NULL, rxsession_pipe_proc, p);
	if (n) {
		np_uerror(n);
		free(p);
		return NULL;
	}

	return p;
}

