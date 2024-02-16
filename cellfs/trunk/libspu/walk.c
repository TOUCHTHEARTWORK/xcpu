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
#include <errno.h>
#include "libspu.h"
#include "spuimpl.h"

int
spc_walk(char *path, int len)
{
	int n, fd;
	u32 nfid;
	char nbuf[128], *ebuf, *cbuf;
	char *s, *epath;
	char *wnames[MAXWELEM];
	Spfcall *fc;
	Spcfid *fid;

	epath = path + len;
	while (*path == '/')
		path++;

	fd = spc_alloc_fid();
	if (fd < 0)
		return -1;

	fid = spc_get_fid(fd);
	fc = spc_get_fcall();
	s = path;
	ebuf = nbuf + sizeof(nbuf) - 1;
	nfid = fs.root->fid;
	while (1) {
		n = 0;
		cbuf = nbuf;
		while (cbuf<ebuf && n<MAXWELEM && s<epath) {
			wnames[n] = cbuf;
			while (cbuf<ebuf && *s!='/' && s<epath)
				*(cbuf++) = *(s++);

			if (cbuf < ebuf) {
				*(cbuf++) = '\0';
				n++;
			}

			if (*s=='/')
				s++;
		}

		if (n==0 && s<epath) {
			sp_werror("file name too long");
			goto error;
		}

		if (sp_create_twalk(fc, nfid, fid->fid, n, wnames) < 0)
			goto error;

		if (spc_rpc(fc) < 0)
			goto error;

		nfid = fid->fid;
		if (fc->nwqid != n) {
			sp_werror("file not found");
			goto error;
		}

		if (s >= epath)
			break;

	}

	return fd;

error:
	if (nfid == fid->fid) {
		if (sp_create_tclunk(fc, nfid) < 0)
			return -1;

		if (!spc_rpc(fc))
			spc_free_fid(fd);
	}

	return -1;
}
