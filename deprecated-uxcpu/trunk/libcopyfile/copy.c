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

static int
copy_dir2dirs(char *dname, int nfiles, Cnpfile *todirs, int perm, int bsize)
{
	int i, ret;
	char *file;
	Cnpfile *tofiles;
	DIR *dir;
	struct dirent *de;

	ret = -1;
	file = NULL;
	tofiles = NULL;
	for(i = 0; i < nfiles; i++) 
		if (mkdir(todirs[i].name, perm) < 0) {
			np_uerror(errno);
			return -1;
		}

	dir = opendir(dname);
	if (!dir) {
		np_uerror(errno);
		return -1;
	}

	tofiles = malloc(nfiles * sizeof(Cnpfile));
	if (!tofiles) {
		np_werror(Enomem, ENOMEM);
		goto done;
	}

	memset(tofiles, 0, nfiles * sizeof(char *));
	while ((de = readdir(dir)) != NULL) {
		if (de->d_name[0] == '.' && (de->d_name[1] == '\0' || de->d_name[1] == '.'))
			continue;
		file = malloc(strlen(dname) + strlen(de->d_name) + 2);
		if (!file) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}
		sprintf(file, "%s/%s", dname, de->d_name);

		for(i = 0; i < nfiles; i++) {
			tofiles[i].name = malloc(strlen(todirs[i].name) + strlen(de->d_name) + 2);
			if (!tofiles[i].name) {
				np_werror(Enomem, ENOMEM);
				goto done;
			}

			sprintf(tofiles[i].name, "%s/%s", todirs[i].name, de->d_name);
		}

		if (copy_file2files(file, nfiles, tofiles, bsize) < 0)
			goto done;

		for(i = 0; i < nfiles; i++) {
			free(tofiles[i].name);
			tofiles[i].name = NULL;
		}
	}

	ret = 0;

done:
	free(file);
	closedir(dir);
	if (tofiles) {
		for(i = 0; i < nfiles; i++)
			free(tofiles[i].name);
		free(tofiles);
	}

	if (dir)
		closedir(dir);

	return ret;
}

static int
copy_fd2fds(int fd, int nfiles, Cnpfile *tofiles, int bsize)
{
	int i, n, m;
	char *buf, *ename;

	buf = malloc(bsize);
	if (!buf) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	while ((n = read(fd, buf, bsize)) > 0) {
		for(i = 0; i < nfiles; i++) {
			m = write(tofiles[i].fd, buf, n);
			if (m != n) {
				if (m < 0) 
					np_uerror(errno);
				else
					np_werror("error while writing", EIO);

				np_rerror(&ename, &tofiles[i].ecode);
				if (ename)
					tofiles[i].ename = strdup(ename);
				return -1;
			}
		}
	}

	if (n < 0) {
		np_uerror(errno);
		return -1;
	}

	return 0;
}

int
copy_file2files(char *fname, int nfiles, Cnpfile *tofiles, int bsize)
{
	int fd, ret;
	u32 i, perm;
	struct stat st;

	if (stat(fname, &st) < 0) {
		np_uerror(errno);
		return -1;
	}

	perm = st.st_mode&0777;
	if (S_ISDIR(st.st_mode)) {
		ret = copy_dir2dirs(fname, nfiles, tofiles, perm, bsize);
	} else {
		for(i = 0; i < nfiles; i++) {
			tofiles[i].ename = NULL;
			tofiles[i].ecode = 0;
			tofiles[i].fd = -1;
		}

		for(i = 0; i < nfiles; i++) {
			tofiles[i].fd = open(tofiles[i].name, O_WRONLY|O_TRUNC|O_CREAT, perm);
			if (tofiles[i].fd < 0) {
				np_uerror(errno);
				goto done;
			}
		}

		fd = open(fname, O_RDONLY);
		if (fd < 0) {
			np_uerror(errno);
			goto done;
		}

		ret = copy_fd2fds(fd, nfiles, tofiles, bsize);
	}

done:
	if (fd < 0)
		close(fd);

	for(i = 0; i < nfiles; i++)
		if (tofiles[i].fd >= 0)
			close(tofiles[i].fd);

	return ret;
}
