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

static int
rx_copy_files(Rxcmd *r, int nrxs, Rxsession **rxs)
{
	int i, n;
	char *pref, *s;
	struct stat st;
	Cnpfile *tofiles;

	if (!r->copypath)
		return 0;

	n = -1;
	if (stat(r->copypath, &st) < 0) {
		np_uerror(errno);
		return -1;
	}

	pref = malloc(strlen(r->copypath) + 8);
	if (!pref) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	if (S_ISDIR(st.st_mode))
		sprintf(pref, "fs");
	else {
		s = strrchr(r->copypath, '/');
		if (!s)
			s = r->copypath;

		sprintf(pref, "fs/%s", s);
	}

	tofiles = malloc(nrxs * sizeof(*tofiles));
	if (!tofiles) {
		free(pref);
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	memset(tofiles, 0, nrxs * sizeof(*tofiles));
	for(i = 0; i < nrxs; i++) {
			tofiles[i].fs = rxs[i]->fsys;
			tofiles[i].name = malloc(strlen(rxs[i]->mntpt) + 
				strlen(rxs[i]->sid) + strlen(pref) + 3);
			if (!tofiles[i].name) {
				np_werror(Enomem, ENOMEM);
				goto done;
			}

			sprintf(tofiles[i].name, "%s/%s/%s", rxs[i]->mntpt, rxs[i]->sid, pref);
	}

	if (r->use9p)
		n = copy_file2npcfiles(r->copypath, nrxs, tofiles, r->blksize);
	else
		n = copy_file2files(r->copypath, nrxs, tofiles, r->blksize);

done:
	free(pref);
	if (tofiles) {
		for(i = 0; i < nrxs; i++) {
			free(tofiles[i].name);
			free(tofiles[i].ename);
		}
	}

	return n;
}

static int
rx_copy_ustr(Rxcmd *r, char *str, char *tofile, int nrxs, Rxsession **rxs)
{
	int i, fd, n;
	char *buf;

	for(i = 0; i < nrxs; i++) {
		buf = malloc(strlen(rxs[i]->sid) + strlen(tofile) + 
			strlen(rxs[i]->mntpt) + 3);
		if (!buf) {
			np_werror(Enomem, ENOMEM);
			return -1;
		}

		sprintf(buf, "%s/%s/%s", rxs[i]->mntpt, rxs[i]->sid, tofile);
		fd = open(buf, O_WRONLY);
		if (fd < 0) {
			np_uerror(errno);
			free(buf);
			return -1;
		}

		free(buf);
		n = write(fd, buf, strlen(buf));
		if (n != strlen(buf)) {
			if (n < 0)
				np_uerror(errno);
			else
				np_werror("error while writing", EIO);
			close(fd);
			return -1;
		}
		close(fd);
	}

	return 0;
}

static int
rx_copy_9str(Rxcmd *r, char *str, char *tofile, int nrxs, Rxsession **rxs)
{
	int i, ret;
	Cnpfile *files;

	ret = -1;
	files = malloc(nrxs * sizeof(*files));
	if (!files) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	memset(files, 0, nrxs * sizeof(*files));
	for(i = 0; i < nrxs; i++) {
		files[i].fs = rxs[i]->fsys;
		files[i].name = malloc(strlen(rxs[i]->sid) + strlen(tofile) + 2);
		if (!files[i].name) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		sprintf(files[i].name, "%s/%s", rxs[i]->sid, tofile);
	}

	ret = copy_buf2npcfiles(str, strlen(str), nrxs, files, 0666);

done:
	for(i = 0; i < nrxs; i++)
		free(files[i].name);

	free(files);
	return ret;
}

static int
rx_copy_str(Rxcmd *r, char *str, char *tofile, int nrxs, Rxsession **rxs)
{
	if (r->use9p)
		return rx_copy_9str(r, str, tofile, nrxs, rxs);
	else
		return rx_copy_ustr(r, str, tofile, nrxs, rxs);
}

static int
rx_writectl9(int nrx, Rxsession **rxs, char **ctls)
{
	int i, ret;
	Cnpfile *cfiles;
	int *ctlens;

	ret = -1;
	cfiles = malloc(nrx * sizeof(*cfiles));
	if (!cfiles) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	ctlens = malloc(nrx * sizeof(int));
	if (!ctlens) {
		free(cfiles);
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	memset(cfiles, 0, nrx * sizeof(*cfiles));
	for(i = 0; i < nrx; i++) {
		cfiles[i].fs = rxs[i]->fsys;
		cfiles[i].name = malloc(strlen(rxs[i]->sid) + 8);
		if (!cfiles[i].name) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		sprintf(cfiles[i].name, "%s/ctl", rxs[i]->sid);
		if (ctls[i])
			ctlens[i] = strlen(ctls[i]);
		else
			ctlens[i] = 0;
	}

	ret = copy_bufs2npcfiles(nrx, ctls, ctlens, cfiles, 0666);

done:
	for(i = 0; i < nrx; i++)
		free(cfiles[i].name);
	free(cfiles);
	free(ctlens);

	return ret;
		
}

static int
rx_writectlu(int nrx, Rxsession **rxs, char **ctls)
{
	np_werror("Not implemented", EIO);
	return -1;
}

static int
rx_clone(Rxcmd *r, Rxsession **rxs, int n, int m)
{
	int i, j, k, b, e;
	int ret;
	char **ctls;

	ret = -1;
	ctls = malloc(n * sizeof(char *));
	if (!ctls) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	memset(ctls, 0, n * sizeof(char *));
	for(i = 0; i < n; i++) {
		b = n + i * (m - 1);
		e = n + (i + 1) * (m - 1);
		if (e > r->nodesnum)
			e = r->nodesnum;

		if (b >= e) {
			ctls[i] = NULL;
			continue;
		}
     
		k = 0;
		for(j = b; j < e; j++) 
			k += strlen(r->nodes[j]) + strlen(rxs[j]->sid) + 2;

		ctls[i] = malloc(k + 64);
		if (!ctls[i]) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		sprintf(ctls[i], "clone %d ", r->maxsessions);
		for(j = b; j < e; j++) {
			strcat(ctls[i], rxs[j]->addr);
			strcat(ctls[i], "/");
			strcat(ctls[i], rxs[j]->sid);
			strcat(ctls[i], ",");
		}

		ctls[i][strlen(ctls[i]) - 1] = '\n';
	}

	if (r->use9p)
		ret = rx_writectl9(n, rxs, ctls);
	else
		ret = rx_writectlu(n, rxs, ctls);

done:
	if (ctls) {
		for(i = 0; i < n; i++)
			free(ctls[i]);
		free(ctls);
	}

	return ret;
}

int
rxcmd(Rxcmd *r)
{
	int i, n, m, ret;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	Rxsession **rxs;
	char *exec;

	ret = -1;
	exec = NULL;
	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	rxs = malloc(r->nodesnum * sizeof(*rxs));
	if (!rxs) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	for(i = 0; i < r->nodesnum; i++) {
		if (r->use9p)
			rxs[i] = rxsession_9create(r->nodes[i], r->stdoutfd, 
				r->stderrfd, r->stdinfd, &lock, &cond);
		else
			rxs[i] = rxsession_create(r->nodes[i], r->stdoutfd,
				r->stderrfd, r->stdinfd, &lock, &cond);

		if (!rxs[i]) {
			free(rxs);
			return -1;
		}
	}

	if (r->maxsessions > 0) {
		n = r->maxsessions;
		if (n > r->nodesnum)
			n = r->nodesnum;
		m = r->nodesnum/n + (r->nodesnum%n?1:0);
	} else {
		n = r->nodesnum;
		m = 1;
	}

	if (rx_copy_files(r, n, rxs) < 0)
		goto error;

	if (r->env && rx_copy_str(r, r->env, "env", n, rxs) < 0)
		goto error;

	if (r->argv && rx_copy_str(r, r->argv, "argv", n, rxs) < 0)
		goto error;


	if (m>1 && rx_clone(r, rxs, n, m) < 0)
		goto error;

	exec = malloc(strlen(r->exec) + 16);
	if (!exec)
		goto error;

	sprintf(exec, "exec %s\n", r->exec);
	for(i = 0; i < r->nodesnum; i++) {
		if (rxfile_write(rxs[i]->ctl, exec, strlen(exec)) < 0) 
			goto error;
	}

	ret = 0;

done:
	free(exec);
	pthread_mutex_lock(&lock);
	while (1) {
		for(i = 0; i < r->nodesnum; i++) 
			if (!rxs[i]->finished)
				break;

		if (i >= r->nodesnum)
			break;

		pthread_cond_wait(&cond, &lock);
	}
	pthread_mutex_unlock(&lock);

	for(i = 0; i < r->nodesnum; i++)
		rxsession_destroy(rxs[i]);

	return ret;

error:
	for(i = 0; i < r->nodesnum; i++) 
		rxsession_wipe(rxs[i]);

	goto done;
}
