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
npcwritetree(Tfile *src, Npcfsys *fs, char *todir)
{
	int i, l, m, n, sfd;
	Npcfid *fid, *sfid;
	u8 *buf;
	char *dstname;
	Npwstat *de;
	Tfile *c;

	fid = NULL;
	buf = NULL;
	dstname = malloc(strlen(todir) + strlen(src->de->name) + 2);
	if (!dstname) {
		np_werror(Enomem, ENOMEM);
		return -1;
	}

	sprintf(dstname, "%s/%s", todir, src->de->name);
	de = npc_stat(fs, dstname);
	if (de && ((de->mode&Dmdir) != (src->de->mode&Dmdir))) {
		np_werror("file exist and different type", EEXIST);
		goto error;
	}

	fid = NULL;
	if (!de) {
		fid = npc_create(fs, dstname, src->de->mode, Owrite);
		if (!fid) 
			goto error;
	}

	if (src->de->mode & Dmdir) {
		for(c = src->dir; c != NULL; c = c->next)
			if (npcwritetree(c, fs, dstname) < 0)
				goto error;
	} else {
		if (!fid) {
			fid = npc_open(fs, dstname, Owrite | Otrunc);
			if (!fid) 
				goto error;
		}

		if (src->data) {
			n = 0;
			while ((i = npc_write(fid, src->data + n, src->de->length - n, n)) > 0)
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
					while ((i = npc_write(fid, buf+m, n-m, l+m)) > 0)
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

				l = 0;
				while ((n = read(sfd, buf, Bufsize)) > 0) {
					m = 0;
					while ((i = npc_write(fid, buf+m, n-m, l+m)) > 0)
						m += i;

					if (m != n) {
						np_werror("error while writing", EIO);
						free(buf);
						close(sfd);
						goto error;
					}

					l += n;
				}
				close(sfd);
			}

			free(buf);
		}
	}

	npc_close(fid);
	free(dstname);
	free(de);	
	return 0;

error:
	if (fid)
		npc_close(fid);

	free(de);	
	free(dstname);
	return -1;
}

