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
#include <string.h>
#include <stdio.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include "libspu.h"
#include "spuimpl.h"

static int spc_clunk(int fd);

int
spc_create(char *path, u32 perm, int mode)
{
	int fd;
	char *fname;
	Spfcall *fc;
	Spcfid *fid;

	fd = spc_open(path, mode | Otrunc);
	if (fd >= 0)
		return fd;

	sp_werror(NULL);
	fname = strrchr(path, '/');
	if (!fname)
		fname = path;

	fd = spc_walk(path, fname - path);
	if (fd < 0)
		return -1;

	fid = spc_get_fid(fd);
	if (*fname == '/')
		fname++;

	fc = spc_get_fcall();
	if (sp_create_tcreate(fc, fid->fid, fname, perm, mode) < 0)
		goto error;

	if (spc_rpc(fc) < 0)
		goto error;

	fid->offset = 0;
	fid->iounit = fc->iounit;
	if (!fid->iounit || fid->iounit>fs.msize-IOHDRSZ)
		fid->iounit = fs.msize-IOHDRSZ;

	if (fc->qid.type & Qtmem) {
		fid->hptr = fc->qid.path;
		spc_dma_update(fid);
	} else 
		fid->hptr = 0;

	return fd;

error:
	spc_clunk(fd);
	return -1;
}

int
spc_open(char *path, int mode)
{
	int fd;
	Spfcall *fc;
	Spcfid *fid;

	fd = spc_walk(path, strlen(path));
	if (fd < 0)
		return -1;

	fid = spc_get_fid(fd);
	fc = spc_get_fcall();
	if (sp_create_topen(fc, fid->fid, mode) < 0)
		goto error;

	if (spc_rpc(fc) < 0)
		goto error;

	fid->offset = 0;
	fid->iounit = fc->iounit;
	if (!fid->iounit || fid->iounit>fs.msize-IOHDRSZ)
		fid->iounit = fs.msize-IOHDRSZ;

	if (fc->qid.type & Qtmem) {
		fid->hptr = fc->qid.path;
		spc_dma_update(fid);
	} else
		fid->hptr = 0;

	return fd;

error:
	spc_clunk(fd);
	return -1;
}

static int
spc_clunk(int fd)
{
	Spfcall *fc;
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (!fid)
		return -1;

	fc = spc_get_fcall();
	if (sp_create_tclunk(fc, fid->fid) < 0)
		goto error;

	if (spc_rpc(fc) < 0) 
		goto error;

	spc_free_fid(fd);
	return 0;

error:
	return -1;
}

int
spc_close(int fd)
{
	return spc_clunk(fd);
}
