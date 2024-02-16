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

#define MFILE_SIZE(p)	*((u64 *)(p))
#define MFILE_MTIME(p)	*(u32 *)((u8 *)(p) + 8)
#define MFILE_FLAGS(p)	*(u32 *)((u8 *)(p) + 12)
#define MFILE_DPTR(p)	*(u64 *)((u8 *)(p) + 16)
#define MFILE_DSIZE(p)	*(u64 *)((u8 *)(p) + 32)

int
spc_dma_update(Spcfid *fid)
{
	u64 hptr;
	Spfcall *fc;

	fc = spc_get_fcall();

	fs.dmamask |= 0x2 << (corid()*2);
	hptr = fid->hptr;
	spu_mfcdma64(fc->pkt, mfc_ea2h(hptr), mfc_ea2l(hptr), 128, corid()*2 + 1, MFC_GET_CMD);
	corwait();
	fid->dmaflags = MFILE_FLAGS(fc->pkt);
	fid->dptr = MFILE_DPTR(fc->pkt);
	fid->dsize = MFILE_DSIZE(fc->pkt);

//	spc_log("dma_update id %d fid %d %lx size %lx\n", id, fid->fid, fid->dptr, fid->dsize);
	return 0;
}

int
spc_dma_read(Spcfid *fid, u8 *buf, u32 count, u64 offset)
{
	u32 i, n;
	u64 hptr, ptr, length;
	Spfcall *fc;

	if (offset%16!=0 && count%16!=0 && ((unsigned int) buf)%16!=0)
		return 0;

	if (offset+count > fid->dsize)
		return 0;

//	fprintf(stderr, "dma_read fid %d offset %lld\n", fid->fid, offset);
//	spc_log("dma read fid %d corid %d hptr %lx ptr %lx buf %p count %d\n", fid->fid, corid(), fid->hptr, fid->dptr + offset, buf, count);
	fc = spc_get_fcall();
	fs.dmamask |= 0x2 << (corid() * 2);
	n = 0;
	hptr = fid->hptr;
	ptr = fid->dptr + offset;
//	spc_log("get header\n");
	spu_mfcdma64(fc->pkt, mfc_ea2h(hptr), mfc_ea2l(hptr), 128, corid()*2 + 1, MFC_GET_CMD);
//	spc_log("done get header\n");

	while (n < count) {
		i = count - n;
		if (i > 16384)
			i = 16384;

		fs.dmamask |= 1 << (corid() * 2);
		spu_mfcdma64(buf, mfc_ea2h(ptr), mfc_ea2l(ptr), i, corid()*2, MFC_GET_CMD);
//		fprintf(stderr, "before corwait\n");
		corwait();
		n += i;
		ptr += i;
		buf += i;

		length = MFILE_SIZE(fc->pkt);
		if (offset+n > length) {
			n = length - offset;
			if (n < 0)
				n = 0;
			break;
		}
	}
//	spc_log("dma read buf %p done\n", buf);

//	fprintf(stderr, "dma_read fid %d offset %lld done\n", fid->fid, offset);
	return n;
}

int
spc_dma_write(Spcfid *fid, u8 *buf, u32 count, u64 offset)
{
	u32 i, n;
	u64 hptr, ptr, length;
	Spfcall *fc;

	if (offset%16!=0 || count%16!=0 || ((unsigned int) buf)%16!=0)
		return 0;

	if (count+offset > fid->dsize)
		return 0;

	fc = spc_get_fcall();
	fs.dmamask |= 0x3 << (corid() * 2);
	n = 0;
	hptr = fid->hptr;
	ptr = fid->dptr + offset;
	spu_mfcdma64(fc->pkt, mfc_ea2h(hptr), mfc_ea2l(hptr), 128, corid()*2 + 1, MFC_GET_CMD);
	while (n < count) {
		i = count - n;
		if (i > 16384)
			i = 16384;

		spu_mfcdma64(buf, mfc_ea2h(ptr), mfc_ea2l(ptr), i, corid()*2, MFC_PUT_CMD);
		corwait();
		n += i;
		ptr += i;
	}

	length = MFILE_SIZE(fc->pkt);
	if (length < offset + count) {
		// TODO: do proper locking
		MFILE_SIZE(fc->pkt) = offset + count;
		fs.dmamask |= 0x2 << (corid() * 2);
		spu_mfcdma64(fc->pkt, mfc_ea2h(hptr), mfc_ea2l(hptr), 128, corid()*2 + 1, MFC_PUT_CMD);
		corwait();
	}

	return count;
}
