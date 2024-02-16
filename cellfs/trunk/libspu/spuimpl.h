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

typedef struct Spstr Spstr;
typedef struct Spqid Spqid;
typedef struct Spstat Spstat;
typedef struct Spfcall Spfcall;
typedef struct Spfid Spfid;
typedef struct Spbuf Spbuf;
typedef struct Sptrans Sptrans;
typedef struct Spconn Spconn;
typedef struct Spreq Spreq;
typedef struct Spwthread Spwthread;
typedef struct Spauth Spauth;
typedef struct Spsrv Spsrv;
typedef struct Spgroup Spgroup;
typedef struct Spfile Spfile;
typedef struct Spfilefid Spfilefid;
typedef struct Spfileops Spfileops;
typedef struct Spdirops Spdirops;
typedef struct Spfd Spfd;
typedef struct Spcfid Spcfid;
typedef struct Spcfsys Spcfsys;
typedef struct Spcreq Spcreq;
typedef struct Spcpool Spcpool;
typedef struct Label Label;

enum {
	Maxcor = 8,
	Maxfid = 16,
	Msize = 1024,
};

/* message types */
enum {
	Tfirst		= 100,
	Tversion	= 100,
	Rversion,
	Tauth		= 102,
	Rauth,
	Tattach		= 104,
	Rattach,
	Terror		= 106,
	Rerror,
	Tflush		= 108,
	Rflush,
	Twalk		= 110,
	Rwalk,
	Topen		= 112,
	Ropen,
	Tcreate		= 114,
	Rcreate,
	Tread		= 116,
	Rread,
	Twrite		= 118,
	Rwrite,
	Tclunk		= 120,
	Rclunk,
	Tremove		= 122,
	Rremove,
	Tstat		= 124,
	Rstat,
	Twstat		= 126,
	Rwstat,
	Rlast
};

#define NOTAG		(u16)(~0)
#define NOFID		(u32)(~0)
#define MAXWELEM	16
#define IOHDRSZ		24

struct Spqid {
	u8		type;
	u32		version;
	u64		path;
};

struct Spstat {
	u16 		size;
	u16 		type;
	u32 		dev;
	Spqid		qid;
	u32 		mode;
	u32 		atime;
	u32 		mtime;
	u64 		length;
	char*		name;
	char*		uid;
	char*		gid;
	char*		muid;
};

struct Test {
	union {
		struct {
			int a;
			int b;
		};

		struct {
			int c;
			int d;
		};
	};
};
	

struct Spfcall {
	u32		size;
	u8		type;
	u16		tag;
	u8*		pkt;
	u32		fid;

	union {
		struct {
			u32		msize;			/* Tversion, Rversion */
			char*		version;		/* Tversion, Rversion */
		};

		struct {
			u32		afid;			/* Tauth, Tattach */
			char*		uname;			/* Tauth, Tattach */
			char*		aname;			/* Tauth, Tattach */
		};

		struct {
			Spqid		aqid;			/* Rauth */
		};

		struct {
			Spqid		qid;			/* Rattach, Ropen, Rcreate */
			u32		iounit;			/* Ropen, Rcreate */
		};

		struct {
			char*		ename;			/* Rerror */
		};

		struct {
			u16		oldtag;			/* Tflush */
		};

		struct {
			u32		newfid;			/* Twalk */
			u16		nwname;			/* Twalk */
			char*		wnames[MAXWELEM];	/* Twalk */
		};

		struct {
			u16		nwqid;			/* Rwalk */
			Spqid		wqids[MAXWELEM];	/* Rwalk */
		};

		struct {
			u8		mode;			/* Topen, Tcreate */
			char*		name;			/* Tcreate */
			u32		perm;			/* Tcreate */
		};

		struct {
			u64		offset;			/* Tread, Twrite */
			u32		count;			/* Tread, Rread, Twrite, Rwrite */
			u8*		data;			/* Rread, Twrite */
		};

		struct {
			Spstat		stat;			/* Rstat, Twstat */
		};
	};
};

struct Spcfid {
	u32		iounit;
	u32		fid;
	u64		offset;
	u32		dmaflags;
	u64		hptr;
	u64		dptr;
	u64		dsize;
};

struct Label {
	unsigned char label[16*50];
};

struct Spcpool {
	u32		maxid;
	int		msize;
	u8*		map;
};

struct Spcfsys {
	u32		msize;
	Spcfid*		root;
	u32		dmamask;
};

extern Spcfsys fs;

/* np.c */
int sp_deserialize(Spfcall*, u8*);
int sp_serialize_stat(Spstat *stat, u8* buf, int buflen);
int sp_deserialize_stat(Spstat *stat, u8* buf, int buflen);

int sp_strcmp(Spstr *str, char *cs);
int sp_strncmp(Spstr *str, char *cs, int len);

void sp_set_tag(Spfcall *, u16);
int sp_create_tversion(Spfcall *, u32 msize, char *version);
int sp_create_tflush(Spfcall *, u16 oldtag);
int sp_create_tattach(Spfcall *, u32 fid, u32 afid, char *uname, char *aname);
int sp_create_twalk(Spfcall *, u32 fid, u32 newfid, u16 nwname, char **wnames);
int sp_create_topen(Spfcall *, u32 fid, u8 mode);
int sp_create_tcreate(Spfcall *, u32 fid, char *name, u32 perm, u8 mode);
int sp_create_tread(Spfcall *, u32 fid, u64 offset, u32 count);
int sp_create_twrite(Spfcall *, u32 fid, u64 offset, u32 count, u8 *data);
int sp_create_tclunk(Spfcall *, u32 fid);
int sp_create_tremove(Spfcall *, u32 fid);
int sp_create_tstat(Spfcall *, u32 fid);
int sp_create_twstat(Spfcall *, u32 fid, Spstat *wstat);

/* error.c */
void sp_werrorstr(Spstr *str);

/* fsys.c */
int spc_init(void);
int spc_mount(char *aname, char *uname, u32 n_uname);
Spfcall *spc_get_fcall(void);
int spc_rpc(Spfcall *tc);
void spc_check(int block);
int spc_alloc_fid(void);
void spc_free_fid(int);
Spcfid *spc_get_fid(int);
void spc_put_fid(Spcfid *fid);
int spc_walk(char *path, int plen);
//int sp_printfcall(FILE *f, Spfcall *fc);

/* cor.c */
void fakelabel(Label *, void (*fp)(void *), void *stackbase, int stacksize);
void gotolabel(Label *);
int setlabel(Label *);
void corwait();
void corready(int corid);

/* log.c */
int spc_log_init();

/* dma.c */
int spc_dma_update(Spcfid *fid);
int spc_dma_read(Spcfid *fid, u8 *buf, u32 count, u64 offset);
int spc_dma_write(Spcfid *fid, u8 *buf, u32 count, u64 offset);

