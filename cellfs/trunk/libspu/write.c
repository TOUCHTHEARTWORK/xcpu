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
#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <stdio.h>
#include "libspu.h"
#include "spuimpl.h"

int
spc_write(int fd, u8 *buf, u32 count)
{
	int n;
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (!fid)
		return -1;

	n = spc_pwrite(fd, buf, count, fid->offset);
	if (n > 0)
		fid->offset += n;

	return n;
}

int
spc_pwrite(int fd, u8 *buf, u32 count, u64 offset)
{
	int i, n;
	Spfcall *fc;
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (!fid)
		return -1;

	if (fid->hptr && count>0) {
		n = spc_dma_write(fid, buf, count, offset);
		if (n > 0)
			return n;
	}

	n = 0;
	while (!count || n < count) {
		i = count - n;
		if (i > fid->iounit)
			i = fid->iounit;

		fc = spc_get_fcall();
		if (sp_create_twrite(fc, fid->fid, offset + n, i, buf) < 0)
			goto error;

		if (spc_rpc(fc) < 0)
			goto error;

		if (!fc->count)
			break;

		n += fc->count;
		if (!count)
			break;
	}

	if (fid->hptr)
		spc_dma_update(fid);

	return n;

error:
	return -1;
}

