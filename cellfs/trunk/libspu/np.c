/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libspu.h"
#include "spuimpl.h"

static u8 *
pint8(u8 *data, u8 val)
{
	data[0] = val;
	return data + 1;
}

static u8 *
pint16(u8 *data, u16 val)
{
	data[0] = val;
	data[1] = val >> 8;
	return data + 2;
}

static u8 *
pint32(u8 *data, u32 val)
{
	data[0] = val;
	data[1] = val >> 8;
	data[2] = val >> 16;
	data[3] = val >> 24;
	return data + 4;
}

static u8 *
pint64(u8 *data, u64 val)
{
	data[0] = val;
	data[1] = val >> 8;
	data[2] = val >> 16;
	data[3] = val >> 24;
	data[4] = val >> 32;
	data[5] = val >> 40;
	data[6] = val >> 48;
	data[7] = val >> 56;
	return data + 8;
}

static u8 *
pstr(u8 *data, char *s)
{
	int n;

	if (!s)
		pint16(data, 0);
	else {
		n = strlen(s);
		data = pint16(data, n);
		memmove(data, s, n);
		data += n;
	}

	return data;
}

static u8 *
pqid(u8 *data, Spqid *q)
{
	data = pint8(data, q->type);
	data = pint32(data, q->version);
	data = pint64(data, q->path);

	return data;
}

static u8 *
pstat(u8 *data, Spstat *wstat)
{
	u8 *p;

	p = data;
	data += 2;	/* set size at the end */
	data = pint16(data, wstat->type);
	data = pint32(data, wstat->dev);
	data = pqid(data, &wstat->qid);
	data = pint32(data, wstat->mode);
	data = pint32(data, wstat->atime);
	data = pint32(data, wstat->mtime);
	data = pint64(data, wstat->length);
	data = pstr(data, wstat->name);
	data = pstr(data, wstat->uid);
	data = pstr(data, wstat->gid);
	data = pstr(data, wstat->muid);
	pint16(p, data - p - 2);

	return data;
}

static u8 *
gint8(u8 *data, u8 *val)
{
	*val = data[0];
	return data + 1;
}

static u8 *
gint16(u8 *data, u16 *val)
{
	*val = data[0] | (data[1]<<8);
	return data + 2;
}

static u8 *
gint32(u8 *data, u32 *val)
{
	*val = data[0] | (data[1]<<8) | (data[2]<<16) | (data[3]<<24);
	return data + 4;
}

static u8 *
gint64(u8 *data, u64 *val)
{
	*val = (u64)data[0] | ((u64)data[1]<<8) | ((u64)data[2]<<16) | 
		((u64)data[3]<<24) | ((u64)data[4]<<32) | ((u64)data[5]<<40) | 
		((u64)data[6]<<48) | ((u64)data[7]<<56);
	return data + 8;
}

static u8 *
gstr(u8 *data, char **s)
{
	u16 n;

	data = gint16(data, &n);
	if (n) {
		memmove(data, data, n);
		data[n] = '\0';
		*s = (char *) data;
	} else
		*s = NULL;

	data += 2 + n;
	return data;
}

static u8 *
gqid(u8 *data, Spqid *q)
{
	data = gint8(data, &q->type);
	data = gint32(data, &q->version);
	data = gint64(data, &q->path);

	return data;
}

static u8 *
gstat(u8 *data, Spstat *stat)
{
	data = gint16(data, &stat->size);
	data = gint16(data, &stat->type);
	data = gint32(data, &stat->dev);
	data = gqid(data, &stat->qid);
	data = gint32(data, &stat->mode);
	data = gint32(data, &stat->atime);
	data = gint32(data, &stat->mtime);
	data = gint64(data, &stat->length);
	data = gstr(data, &stat->name);
	data = gstr(data, &stat->uid);
	data = gstr(data, &stat->gid);
	data = gstr(data, &stat->muid);

	return data;
}

void
sp_set_tag(Spfcall *fc, u16 tag)
{
	pint16(&fc->pkt[5], tag);
	fc->tag = tag;
}

static void
ptype(Spfcall *fc, u8 type)
{
	fc->type = type;
	pint8(fc->pkt + 4, type);
}

static void
psize(Spfcall *fc, u32 size)
{
	fc->size = size;
	pint32(fc->pkt, size);
}

int
sp_create_tversion(Spfcall *fc, u32 msize, char *version)
{
	u8* data;

	ptype(fc, Tversion);
	data = fc->pkt + 7;

	data = pint32(data, msize);
	data = pstr(data, version);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tauth(Spfcall *fc, u32 fid, char *uname, char *aname)
{
	u8* data;

	ptype(fc, Tauth);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pstr(data, uname);
	data = pstr(data, aname);
	
	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tflush(Spfcall *fc, u16 oldtag)
{
	u8* data;

	ptype(fc, Tflush);
	data = fc->pkt + 7;

	data = pint16(data, oldtag);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tattach(Spfcall *fc, u32 fid, u32 afid, char *uname, char *aname)
{
	u8* data;

	ptype(fc, Tattach);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pint32(data, afid);
	data = pstr(data, uname);
	data = pstr(data, aname);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_twalk(Spfcall *fc, u32 fid, u32 newfid, u16 nwname, char **wnames)
{
	int i;
	u8* data;

	ptype(fc, Twalk);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pint32(data, newfid);
	data = pint16(data, nwname);
	for(i = 0; i < nwname; i++)
		data = pstr(data, wnames[i]);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_topen(Spfcall *fc, u32 fid, u8 mode)
{
	u8* data;

	ptype(fc, Topen);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pint8(data, mode);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tcreate(Spfcall *fc, u32 fid, char *name, u32 perm, u8 mode)
{
	u8* data;

	ptype(fc, Tcreate);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pstr(data, name);
	data = pint32(data, perm);
	data = pint8(data, mode);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tread(Spfcall *fc, u32 fid, u64 offset, u32 count)
{
	u8* data;

	ptype(fc, Tread);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pint64(data, offset);
	data = pint32(data, count);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_twrite(Spfcall *fc, u32 fid, u64 offset, u32 count, u8 *dat)
{
	u8* data;

	ptype(fc, Twrite);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	data = pint64(data, offset);
	data = pint32(data, count);
	memmove(data, dat, count);
	data += count;
	
	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tclunk(Spfcall *fc, u32 fid)
{
	u8* data;

	ptype(fc, Tclunk);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tremove(Spfcall *fc, u32 fid)
{
	u8* data;

	ptype(fc, Tremove);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_tstat(Spfcall *fc, u32 fid)
{
	u8* data;

	ptype(fc, Tstat);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_create_twstat(Spfcall *fc, u32 fid, Spstat *stat)
{
	u8* data;
	u8* p;

	ptype(fc, Twstat);
	data = fc->pkt + 7;

	data = pint32(data, fid);
	p = data;
	data += 2;			/* + statsz */
	data = pstat(data, stat);
	pint16(p, data - p + 2);

	psize(fc, data - fc->pkt);
	return 0;
}

int
sp_deserialize(Spfcall *fc, u8 *data)
{
	int i;

	data = gint32(data, &fc->size);
	data = gint8(data, &fc->type);
	data = gint16(data, &fc->tag);
	fc->fid = fc->afid = fc->newfid = NOFID;

	switch (fc->type) {
	default:
		return -1;

	case Rversion:
		data = gint32(data, &fc->msize);
		data = gstr(data, &fc->version);
		break;

	case Rauth:
		data = gqid(data, &fc->aqid);
		break;

	case Rattach:
		data = gqid(data, &fc->qid);
		break;

	case Rerror:
		data = gstr(data, &fc->ename);
		break;

	case Rwalk:
		data = gint16(data, &fc->nwqid);
		for(i = 0; i < fc->nwqid; i++)
			data = gqid(data, &fc->wqids[i]);
		break;

	case Ropen:
	case Rcreate:
		data = gqid(data, &fc->qid);
		data = gint32(data, &fc->iounit);
		break;

	case Rread:
		data = gint32(data, &fc->count);
		fc->data = data;
		data += fc->count;
		break;

	case Rwrite:
		data = gint32(data, &fc->count);
		break;

	case Rflush:
	case Rclunk:
	case Rremove:
	case Rwstat:
		break;

	case Rstat:
		data += 2;
		data = gstat(data, &fc->stat);
		break;
	}

	return fc->size;
}
