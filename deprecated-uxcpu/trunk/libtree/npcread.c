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

Tfile *
npcreadtree(Npcfsys *fs, char *file, int load)
{
	int i, n;
	char *s;
	Tfile *ret, *f;
	Npwstat *ds;
	Npcfid *fid;

	ret = treealloc(file);
	if (!ret)
		return NULL;

	ret->fs = fs;
	ret->de = npc_stat(fs, file);
	fid = npc_open(fs, file, Oread);
	if (!fid)
		goto error;

	if (ret->de->mode & Dmdir) {
		while ((n = npc_dirread(fid, &ds)) > 0) {
			for(i = 0; i < n; i++) {
				s = malloc(strlen(file) + strlen(ds[i].name) + 2);
				if (!s) {
					free(ds);
					np_werror(Enomem, ENOMEM);
					goto error;
				}

				sprintf(s, "%s/%s", file, ds[i].name);
				f = npcreadtree(fs, s, load);
				if (!f) {
					free(s);
					free(ds);
					goto error;
				}

				f->next = ret->dir;
				ret->dir = f;
				free(s);
			}
			free(ds);
		}
	} else if (load && ret->de->length) {
		ret->data = malloc(ret->de->length);
		if (ret->data) {
			n = 0;
			while ((i = npc_read(fid, ret->data + n, ret->de->length - n, n)) > 0)
				n += i;

			if (n < ret->de->length) {
				free(ret->data);
				ret->data = NULL;
			}
		}
	}

	npc_close(fid);
	return ret;

error:
	if (fid)
		npc_close(fid);

	treefree(ret);
	return NULL;
}
