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

static void
write_cb(void *cba, u32 count)
{
	Cnpfile *tn;
	char *ename;

	tn = cba;
	pthread_mutex_lock(tn->lock);
	np_rerror(&ename, &tn->ecode);
	if (ename) 
		tn->ename = strdup(ename);

	tn->count = count;
	tn->finished = 1;
	pthread_cond_broadcast(tn->cond);
	pthread_mutex_unlock(tn->lock);
}

static int
write_files(char *buf, int buflen, u64 offset, int nfiles, Cnpfile *tofiles, pthread_mutex_t *lock, pthread_cond_t *cond)
{
	int i, j, m;

	for(i = 0; i < nfiles; i++) {
		tofiles[i].lock = lock;
		tofiles[i].cond = cond;
		tofiles[i].ename = NULL;
		tofiles[i].ecode = 0;
		tofiles[i].finished = buflen==0;
		if (buflen > 0) {
			m = npc_writenb(tofiles[i].fid, (u8 *) buf, buflen, offset, write_cb, &tofiles[i]);
			if (m < 0) {
				pthread_mutex_lock(lock);
				for(j = 0; j < i; j++)
					pthread_cond_wait(cond, lock);
				pthread_mutex_unlock(lock);
				np_werror("error while writing", EIO);
				return -1;
			}
		}
	}

	pthread_mutex_lock(lock);
	while (1) {
		for(i = 0; i < nfiles; i++)
			if (!tofiles[i].finished)
				break;

		if (i >= nfiles)
			break;

		pthread_cond_wait(cond, lock);
	}
	pthread_mutex_unlock(lock);

	for(i = 0; i < nfiles; i++)
		if (tofiles[i].ename) {
			np_werror(tofiles[i].ename, tofiles[i].ecode);
			return -1;
		}

	return 0;
}

static int
create_npcfiles(int nfiles, Cnpfile *tofiles, u32 perm)
{
	int i, failed, mode;

	failed = 0;
	if (perm & Dmdir)
		mode = Oread;
	else
		mode = Owrite | Otrunc;

	for(i = 0; i < nfiles; i++) {
		tofiles[i].fid = npc_open(tofiles[i].fs, tofiles[i].name, mode);
		if (!tofiles[i].fid) {
			np_werror(NULL, 0);
			tofiles[i].fid = npc_create(tofiles[i].fs, tofiles[i].name, perm, mode);
		}

		if (!tofiles[i].fid)
			failed++;
	}

	return failed?-1:0;
}

int
copy_buf2npcfiles(char *buf, int buflen, int nfiles, Cnpfile *tofiles, u32 perm)
{
	int i, n;
	pthread_cond_t cond;
	pthread_mutex_t lock;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	n = create_npcfiles(nfiles, tofiles, perm);
	if (n == 0) 
		n = write_files(buf, buflen, 0, nfiles, tofiles, &lock, &cond);

	for(i = 0; i < nfiles; i++)
		if (tofiles[i].fid)
			npc_close(tofiles[i].fid);

	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&cond);
	return n;
}

static int
copy_dir2npcdirs(char *dir, int nfiles, Cnpfile *todirs, int bsize)
{
	int i, ret;
	DIR *d;
	struct dirent *de;
	char *file;
	Cnpfile *tofiles;

       	ret = -1;
	file = NULL;
	tofiles = NULL;
	d = opendir(dir);
	if (!d) {
		np_uerror(errno);
		goto done;
	}

	tofiles = malloc(nfiles * sizeof(Cnpfile));
	if (!tofiles) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	for(i = 0; i < nfiles; i++) {
		tofiles[i].fs = todirs[i].fs;
		tofiles[i].name = NULL;
		tofiles[i].fid = NULL;
		tofiles[i].ename = NULL;
		tofiles[i].ecode = 0;
		tofiles[i].count = 0;
		tofiles[i].cond = NULL;
	}

	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || de->d_name[1] == '.'))
			continue;

		file = malloc(strlen(dir) + strlen(de->d_name) + 2);
		if (!file) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}
		sprintf(file, "%s/%s", dir, de->d_name);

		for(i = 0; i < nfiles; i++) {
			tofiles[i].name = malloc(strlen(todirs[i].name) + strlen(de->d_name) + 2);
			if (!tofiles[i].name) {
				np_werror(Enomem, ENOMEM);
				goto done;
			}
			sprintf(tofiles[i].name, "%s/%s", todirs[i].name, de->d_name);
		}

		if (copy_file2npcfiles(file, nfiles, tofiles, bsize) < 0)
			goto done;

		free(file);
		file = NULL;
		for(i = 0; i < nfiles; i++) {
			free(tofiles[i].name);
			tofiles[i].name = NULL;
		}
	}

	ret = 0;

done:
	if (d)
		closedir(d);

	free(file);
	for(i = 0; i < nfiles; i++)
		free(tofiles[i].name);

	free(tofiles);
	return ret;
}

static int
copy_fd2fids(int fd, int nfiles, Cnpfile *tofiles, int bsize)
{
	int n, ret;
	u64 l;
	char *buf;
	pthread_cond_t cond;
	pthread_mutex_t lock;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	ret = -1;
	buf = malloc(bsize);
	if (!buf) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	l = 0;
	while ((n = read(fd, buf, bsize)) > 0) {
		if (write_files(buf, n, l, nfiles, tofiles, &lock, &cond) < 0) {
			goto done;
		}
		l += n;
	}

	if (n < 0) {
		np_uerror(errno);
		goto done;
	}

	ret = 0;

done:
	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&cond);
	free(buf);
	return ret;
}

int
copy_file2npcfiles(char *fname, int nfiles, Cnpfile *tofiles, int bsize)
{
	int fd, n, ret;
	u32 i, perm;
	struct stat st;

	if (stat(fname, &st) < 0) {
		np_uerror(errno);
		return -1;
	}

	perm = (st.st_mode&0777) | (S_ISDIR(st.st_mode)?Dmdir:0);
	n = create_npcfiles(nfiles, tofiles, perm);
	if (n < 0)
		return -1;

	if (S_ISDIR(st.st_mode)) 
		ret = copy_dir2npcdirs(fname, nfiles, tofiles, bsize);
	else {
		fd = open(fname, O_RDONLY);
		if (fd < 0) {
			np_uerror(errno);
			goto done;
		}

		ret = copy_fd2fids(fd, nfiles, tofiles, bsize);
		close(fd);
	}

done:
	for(i = 0; i < nfiles; i++) {
		if (tofiles[i].fid) {
			npc_close(tofiles[i].fid);
			tofiles[i].fid = NULL;
		}

		free(tofiles[i].ename);
		tofiles[i].ename = NULL;
	}

	return ret;
}

static int
write_bufs(int nfiles, char **bufs, int *buflens, Cnpfile *tofiles, pthread_mutex_t *lock, pthread_cond_t *cond)
{
	int i, j, m, k;

	k = 0;
	for(i = 0; i < nfiles; i++) {
		tofiles[i].lock = lock;
		tofiles[i].cond = cond;
		tofiles[i].ename = NULL;
		tofiles[i].ecode = 0;
		tofiles[i].finished = buflens[i]==0;
		if (buflens[i]) {
			m = npc_writenb(tofiles[i].fid, (u8 *) bufs[i], buflens[i], 0, write_cb, &tofiles[i]);
			if (m < 0) {
				pthread_mutex_lock(lock);
				for(j = 0; j < k; j++)
					pthread_cond_wait(cond, lock);
				pthread_mutex_unlock(lock);
				np_werror("error while writing", EIO);
				return -1;
			}
			k++;
		}
	}

	while (1) {
		for(i = 0; i < nfiles; i++)
			if (!tofiles[i].finished)
				break;

		if (i >= nfiles)
			break;

		pthread_cond_wait(cond, lock);
	}
	pthread_mutex_unlock(lock);

	for(i = 0; i < nfiles; i++)
		if (tofiles[i].ename) {
			np_werror(tofiles[i].ename, tofiles[i].ecode);
			return -1;
		}

	return 0;
}

int
copy_bufs2npcfiles(int nfiles, char **bufs, int *buflens, Cnpfile *tofiles, u32 perm)
{
	int i, n;
	pthread_cond_t cond;
	pthread_mutex_t lock;

	pthread_mutex_init(&lock, NULL);
	pthread_cond_init(&cond, NULL);
	n = create_npcfiles(nfiles, tofiles, perm);
	if (n == 0) 
		n = write_bufs(nfiles, bufs, buflens, tofiles, &lock, &cond);

	for(i = 0; i < nfiles; i++)
		if (tofiles[i].fid)
			npc_close(tofiles[i].fid);

	pthread_mutex_destroy(&lock);
	pthread_cond_destroy(&cond);
	return n;
}
