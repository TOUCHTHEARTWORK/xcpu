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
#include "tree.h"

int
writetree(Tfile *src, char *todir)
{
	int i, m, n, l;
	u8 *buf;
	char *dstname;
	int fd, sfd;
	Npcfid *sfid;
	Npwstat *de;
	Tfile *c;

	buf = NULL;
	fd = -1;
	dstname = malloc(strlen(todir) + strlen(src->de->name) + 2);
	if (!dstname) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	sprintf(dstname, "%s/%s", todir, src->de->name);
	de = filestat(dstname, 1);
	if (de && ((de->mode&Dmdir) != (src->de->mode&Dmdir))) {
		np_werror("file exist and different type", EEXIST);
		goto error;
	}

	if (!de) {
		fd = open(dstname, O_WRONLY | O_CREAT, src->de->mode & 0777);
		if (fd < 0) 
			goto error;
	}

	if (src->de->mode & Dmdir) {
		for(c = src->dir; c != NULL; c = c->next)
			if (writetree(c, dstname) < 0)
				goto error;
	} else {
		if (fd < 0) {
			fd = open(dstname, O_WRONLY|O_TRUNC);
			if (fd < 0) 
				goto error;
		}

		if (src->data) {
			n = 0;
			while ((i = write(fd, src->data + n, src->de->length - n)) > 0)
				n += i;

			if (n < src->de->length) {
				np_werror("error while writing", EIO);
				goto error;
			}
		} else {
			buf = malloc(Bufsize);
			if (!buf) {
				np_werror(Enomem, ENOMEM);
				goto error;
			}

			if (src->fs) {
				sfid = npc_open(src->fs, src->path, Oread);
				if (!sfid) {
					free(buf);
					goto error;
				}

				l = 0;
				while ((n = npc_read(sfid, buf, Bufsize, l)) > 0) {
					m = 0;
					while ((i = write(fd, buf+m, n-m)) > 0)
						m += i;

					if (m != n) {
						np_werror("error while writing", EIO);
						free(buf);
						npc_close(sfid);
						goto error;
					}

					l += n;
				}

				npc_close(sfid);
			} else {
				sfd = open(src->path, O_RDONLY);
				if (sfd < 0) {
					free(buf);
					goto error;
				}

				while ((n = read(sfd, buf, Bufsize)) > 0) {
					m = 0;
					while ((i = write(fd, buf+m, n-m)) > 0)
						m += i;

					if (m != n) {
						np_werror("error while writing", EIO);
						free(buf);
						close(sfd);
						goto error;
					}
				}

				close(sfd);
			}

			free(buf);
		}
	}

	close(fd);
	free(dstname);
	free(de);
	return 0;

error:
	if (fd >= 0)
		close(fd);

	free(de);	
	free(dstname);
	return -1;
}
