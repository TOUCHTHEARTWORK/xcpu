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

int
spc_read(int fd, u8 *buf, u32 count)
{
	int n;
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (!fid)
		return -1;

	n = spc_pread(fd, buf, count, fid->offset);
	if (n > 0)
		fid->offset += n;

	return n;
}

int
spc_pread(int fd, u8 *buf, u32 count, u64 offset)
{
	int n;
	Spfcall *fc;
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (!fid)
		return -1;

	if (fid->hptr && count>0 && count%16==0) {
		n = spc_dma_read(fid, buf, count, offset);
		if (n > 0)
			return n;
	}

	n = count;
	if (n > fid->iounit)
		n = fid->iounit;

	fc = spc_get_fcall();
	if (sp_create_tread(fc, fid->fid, offset, n) < 0)
		goto error;

	if (spc_rpc(fc) < 0)
		goto error;

	n = fc->count;
	if (n > count)
		n = count;
	memmove(buf, fc->data, n);
	return n;

error:
	return -1;
}

