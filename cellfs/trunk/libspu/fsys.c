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

int spc_chatty;
// char *Econn = "connection closed";
static char *Emismatch = "response mismatch";

Spcfsys fs;
static int initialized = 0;
static u8 fcbuf[Maxcor][Msize] __attribute__((aligned(128)));
static Spfcall spc_fcalls[Maxcor];
static Spcfid fids[Maxfid];

int
spc_init(void)
{
	int i, fd;
	char *ename;
	Spfcall *fc;

	if (initialized)
		return 0;

	fs.root = NULL;
	fs.msize = Msize;
	fs.dmamask = 0;

	for(i = 0; i < Maxcor; i++) {
		spc_fcalls[i].pkt = fcbuf[i];
		spc_fcalls[i].size = Msize;
	}

	for(i = 0; i < Maxfid; i++)
		fids[i].fid = NOFID;

	fc = spc_get_fcall();
	if (sp_create_tversion(fc, fs.msize, "9P2000") < 0)
		goto error;

	if (spc_rpc(fc) < 0)
		goto error;

	if (strcmp(fc->version, "9P2000") != 0) {
		sp_werror("unsupported 9P version");
		goto error;
	}

	fs.msize = fc->msize;
	fd = spc_alloc_fid();
	if (fd < 0)
		goto error;

	fs.root = spc_get_fid(fd);
	fc = spc_get_fcall();
	if (sp_create_tattach(fc, fs.root->fid, NOFID, NULL, NULL) < 0)
		goto error;

	if (spc_rpc(fc) < 0)
		goto error;

	initialized = 1;
	return 0;

error:
	sp_rerror(&ename);
//	if (ename)
//		fprintf(stderr, "spc_init error: %s\n", ename);
	return -1;
}

Spfcall *
spc_get_fcall(void)
{
	Spfcall *fc;

	fc = &spc_fcalls[corid()];
	fc->size = fs.msize;
	return fc;
}

int
spc_rpc(Spfcall *fc)
{
	u8 type;
	u32 size;

//	checkstack("spc_rpc1");
	if (fc->type != Tversion)
		sp_set_tag(fc, corid());

/*
	if (spc_chatty) {
		fprintf(stderr, "<-- ");
		sp_printfcall(stderr, fc);
		fprintf(stderr, "\n");
	}
*/

	type = fc->type;
	spu_writech(SPU_WrOutIntrMbox, ((unsigned int) fc->pkt) | (fc->size << 18));
	corwait();
	size = fc->pkt[0] | (fc->pkt[1]<<8) | (fc->pkt[2]<<16) | (fc->pkt[3]<<24);
	if (size > fs.msize) {
		sp_werror("invalid fcall size > msize");
		return -1;
	}

	if (!sp_deserialize(fc, fc->pkt)) {
		sp_werror("invalid fcall");
		return -1;
	}

/*
	if (spc_chatty) {
		fprintf(stderr, "--> ");
		sp_printfcall(stderr, fc);
		fprintf(stderr, "\n");
	}
*/

//	checkstack("spc_rpc2");
	if (fc->type == Rerror) {
		sp_werror(fc->ename);
		return -1;
	} else if (type+1 != fc->type) {
		sp_werror(Emismatch);
		return -1;
	}

	return 0;
}

void
spc_check(int block)
{
	int i, done;
	u32 n, ndma, nsig, mask;

//	fprintf(stderr, "spc_check %d\n", block);
//	checkstack("spc_check");
//	if (fs->dmamask)
		spu_writech(MFC_WrTagMask, fs.dmamask);

again:
	done = 0;
	ndma = spu_mfcstat(MFC_TAG_UPDATE_IMMEDIATE);

	nsig = spu_readchcnt(SPU_RdSigNotify2);
//	fprintf(stderr, "block %d ndma %x nsig %x dmamask %x\n", block, ndma, nsig, fs.dmamask);
	if (!block && !nsig && !ndma) {
//		fprintf(stderr, "spc_check return\n");
		return;
	}

	if (ndma&fs.dmamask) {
		for(i = 0, mask = 3; i < Maxcor; i++, mask <<= 2) {
			n = ndma & mask;
			if (n) {
				fs.dmamask &= ~n;
				if (!(fs.dmamask & mask)) {
//					fprintf(stderr, "dma corready %d\n", i);
					corready(i);
					done = 1;
				}

			}
		}
	}

	if ((block && !done && !fs.dmamask) || nsig) {
//		fprintf(stderr, "wait for signal\n");
		n = spu_readch(SPU_RdSigNotify2);
//		fprintf(stderr, "got signal %x\n", n);
		for(i = 0; i < Maxcor; i++)
			if (n & (1<<i)) {
				done = 1;
				corready(i);
			}
	}

	if (!done)
		goto again;
}

Spcfid *
spc_get_fid(int fd)
{
	if (fd<0 || fd>=Maxfid || fids[fd].fid==NOFID) {
		spc_log("invalid fid %d\n", fd);
		sp_werror("invalid fid");
		return NULL;
	}

	return &fids[fd];
}

int
spc_alloc_fid(void)
{
	int i;

	for(i = 0; i < Maxfid; i++) {
		if (fids[i].fid == NOFID) {
			fids[i].fid = i;
			return i;
		}
	}

	sp_werror("no available fids");
	return -1;
}

void
spc_free_fid(int fd)
{
	Spcfid *fid;

	fid = spc_get_fid(fd);
	if (fid)
		fid->fid = NOFID;
}
