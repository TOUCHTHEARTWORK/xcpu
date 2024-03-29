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
#include "spfs.h"
#include "spclient.h"
#include "spcimpl.h"

Spcfid *
spc_walk(Spcfsys *fs, char *path)
{
	int n;
	u32 nfid;
	char *fname, *s, *t;
	char *wnames[MAXWELEM];
	Spfcall *tc, *rc;
	Spcfid *fid;

	while (*path == '/')
		path++;

	fname = strdup(path);
	fid = spc_fid_alloc(fs);
	s = fname;
	nfid = fs->root->fid;
	while (1) {
		n = 0;
		while (n<MAXWELEM && *s!='\0') {
			if (*s == '/') {
				s++;
				continue;
			}

			wnames[n++] = s;
			t = strchr(s, '/');
			if (!t)
				break;

			*t = '\0';
			s = t + 1;
		}

		tc = sp_create_twalk(nfid, fid->fid, n, wnames);
		if (spc_rpc(fs, tc, &rc) < 0)
			goto error;

		nfid = fid->fid;
		if (rc->nwqid != n) {
			sp_werror("file not found", ENOENT);
			goto error;
		}

		free(tc);
		free(rc);
		if (!t || *s=='\0')
			break;

	}

	free(fname);
	return fid;

error:
	free(rc);
	free(tc);
	if (nfid == fid->fid) {
		tc = sp_create_tclunk(fid->fid);
		if (!spc_rpc(fs, tc, &rc))
			spc_fid_free(fid);

		free(rc);
		free(tc);
	}

	free(fname);
	return NULL;
}
