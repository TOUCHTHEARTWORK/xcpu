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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
//#define _XOPEN_SOURCE 500
#define _BSD_SOURCE
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <assert.h>
#include "spfs.h"
#include "yspufs.h"

#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

#define Fmaxsize (u64) 1*1024*1024*1024

typedef struct Ufid Ufid;
typedef struct Ufile Ufile;
typedef struct Usrv Usrv;

struct Ufid {
	Ufile*		file;
	int		omode;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
};

struct Ufile {
	int		refcount;
	int		removed;
	Usrv*		usrv;
	char*		path;
	struct stat	stat;

	Ufile*		prev;
	Ufile*		next;

	/* dma */
	int		fd;	/* for mmaping */
	u8		header[256];
	u8*		haligned;
	u8*		dptr;
	u64		dsize;
	u8*		dpad;	/* pads dptr to 4GB */
};

struct Usrv {
	int		priv;
	Ufile*		files;
};

static int pagesize;

static Ufile *ufile_find(Usrv *usrv, char *name);
static Ufile *ufile_alloc(Usrv *usrv, char *name);
static Ufile *ufile_walk(Usrv *usrv, Ufile *dir, char *name, int nlen);
static void ufile_incref(Ufile *f);
static void ufile_decref(Ufile *f);
static int ufile_mmap(Ufile *f);
static int ufile_remove(Ufile *file);
static int ufile_stat(Ufile *f);

static Ufid *ufid_alloc();
static void ufid_destroy(Ufid *f);
static int ufid_stat(Ufid *fid);

static int ufid_stat(Ufid *fid);
static void ustat2qid(struct stat *st, Spqid *qid);
static u8 ustat2qidtype(struct stat *st);
static u32 umode2npmode(mode_t umode);
static mode_t npstat2umode(Spstat *st);
static void ustat2npwstat(char *path, struct stat *st, Spwstat *wstat);

static Spfcall* ufs_attach(Spfid *fid, Spfid *afid, Spstr *uname, Spstr *aname);
static int ufs_clone(Spfid *fid, Spfid *newfid);
static int ufs_walk(Spfid *fid, Spstr *wname, Spqid *wqid);
static Spfcall* ufs_open(Spfid *fid, u8 mode);
static Spfcall* ufs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, 
	Spstr *extension);
static Spfcall* ufs_read(Spfid *fid, u64 offset, u32 count, Spreq *);
static Spfcall* ufs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *);
static Spfcall* ufs_clunk(Spfid *fid);
static Spfcall* ufs_remove(Spfid *fid);
static Spfcall* ufs_stat(Spfid *fid);
static Spfcall* ufs_wstat(Spfid *fid, Spstat *stat);

static int ufs_init(char dev, int priv);
static void ufs_fiddestroy(Spfid *fid);

int
ufs2_init(void)
{
	pagesize = getpagesize();
	if (ufs_init('U', 0) < 0)
		return -1;

	if (ufs_init('R', 1) < 0)
		return -1;

	return 0;
}

static int
ufs_init(char dev, int priv)
{
	Usrv *usrv;
	Spsrv *srv;


	usrv = sp_malloc(sizeof(*usrv));
	if (!usrv)
		return -1;

	usrv->priv = priv;
	usrv->files = NULL;
	srv = sp_srv_create();
	if (!srv)
		return -1;

	srv->treeaux = usrv;
	srv->dotu = 0;
	srv->debuglevel = 0;
	srv->attach = ufs_attach;
	srv->clone = ufs_clone;
	srv->walk = ufs_walk;
	srv->open = ufs_open;
	srv->create = ufs_create;
	srv->read = ufs_read;
	srv->write = ufs_write;
	srv->clunk = ufs_clunk;
	srv->remove = ufs_remove;
	srv->stat = ufs_stat;
	srv->wstat = ufs_wstat;
	srv->fiddestroy = ufs_fiddestroy;

	return register_dev(dev, srv);
}

static Ufile *
ufile_find(Usrv *usrv, char *name)
{
	Ufile *f;

	for(f = usrv->files; f != NULL; f = f->next)
		if (strcmp(f->path, name) == 0)
			return f;

	return NULL;
}

static Ufile *
ufile_alloc(Usrv *usrv, char *name)
{
	u64 m;
	Ufile *f;

	f = sp_malloc(sizeof(*f));
	if (!f)
		return NULL;

	f->refcount = 1;
	f->removed = 0;
	f->fd = -1;
	f->path = name;
	f->usrv = usrv;
	f->haligned = NULL;
	f->dptr = NULL;
	f->dpad = NULL;
	m = (u64) f->header;
	m += m%128?128-m%128:0;
	f->haligned = (u8 *) m;
	f->next = NULL;
	f->prev = NULL;

	if (usrv) {
		f->next = usrv->files;
		if (usrv->files)
			usrv->files->prev = f;
		usrv->files = f;
	}

	return f;
}

static Ufile *
ufile_walk(Usrv *usrv, Ufile *dir, char *name, int nlen)
{
	int n;
	char *path;
	struct stat st;
	Ufile *f;

	f = NULL;
	n = dir?strlen(dir->path):0;
	if (n==1 && *dir->path=='/')
		n = 0;

	path = sp_malloc(n + nlen + 2);
	if (!path)
		return NULL;

	if (dir)
		memmove(path, dir->path, n);
	path[n] = '/';
	memmove(path + n + 1, name, nlen);
	path[n + nlen + 1] = '\0';

	if (dir) {
		f = ufile_find(dir->usrv, path);
		if (f) {
			ufile_incref(f);
			goto done;
		}
	}

	if (stat(path, &st) < 0) {
		sp_uerror(errno);
		goto done;
	}

	f = ufile_alloc(usrv, path);
	if (!f)
		goto done;

	if (usrv->priv)
		ufile_incref(f);

	path = NULL;
	f->stat = st;
	if (!S_ISREG(st.st_mode))
		goto done;

	f->fd = open(f->path, O_RDWR);
	if (f->fd < 0) {
		f->fd = open(f->path, O_RDONLY);
		if (f->fd < 0) {
			sp_uerror(errno);
			ufile_decref(f);
			goto done;
		}
	}

	ufile_mmap(f);

done:
	free(path);
	return f;
}

static void
ufile_incref(Ufile *f)
{
	assert(f->refcount > 0);
	f->refcount++;
}

static void
ufile_decref(Ufile *f)
{
	f->refcount--;
	assert(f->refcount >= 0);
	if (f->refcount == 0) {
//		fprintf(stderr, "ufile_destroy %s\n", f->path);
		if (!f->removed) {
			if (f->prev)
				f->prev->next = f->next;
			else
				f->usrv->files = f->next;

			if (f->next)
				f->next->prev = f->prev;
		}

		if (f->dptr) {
//			if (munmap(f->dptr, f->dsize) < 0)
//				sp_uerror(errno);
		}

		if (f->dpad) {
//			if (munmap(f->dpad, Fmaxsize) < 0) {
//				fprintf(stderr, "munmap dpad failed\n");
//				sp_uerror(errno);
//			}
		}

		if (f->fd >= 0)
			close(f->fd);

//		fprintf(stderr, "ufile_decref middle\n");
		free(f->path);
		free(f);
//		fprintf(stderr, "ufile_decref end\n");
	}
}

static int
ufile_mmap(Ufile *f)
{
	int flags, prot;
	u64 size, sz;
	u8 *p, *dp, *ep;

//	fprintf(stderr, "ufile_mmap %s hptr %p\n", f->path, f->haligned);
	if (!f->dpad) {
		p = mmap(0, Fmaxsize, PROT_READ | PROT_WRITE, 
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (p == MAP_FAILED) {
			sp_uerror(errno);
			return -1;
		}

		f->dpad = p;
	}

	/* TODO: support for shrinking files */
	dp = f->dpad;
	size = f->stat.st_size;

	/* first munmap part of the padding */
	if (f->dptr)
		ep = f->dptr + size;
	else
		ep = f->dpad + size;

	if (ep > f->dpad) {
		sz = ep - f->dpad;
		sz += sz%pagesize?pagesize-sz%pagesize:0;
		if (sz && munmap(f->dpad, sz) < 0) {
			sp_uerror(errno);
			return -1;
		}
		f->dpad += sz;
	} else if (ep < f->dpad) {
		munmap(f->dpad, Fmaxsize - f->dsize);
		sz = size;
		sz += sz%pagesize?pagesize-sz%pagesize:0;
		p = mmap(f->dptr + sz, Fmaxsize - sz, PROT_READ | PROT_WRITE, 
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);

		if (p == MAP_FAILED) {
			sp_uerror(errno);
			return -1;
		}

		f->dpad = p;
	}

	if (f->dptr) {
		/* then try to increase the dptr mapping */
		if (f->dsize!=size && mremap(f->dptr, f->dsize, size, 0) < 0) {
			sp_uerror(errno);
			return -1;
		}
		f->dsize = size;
	} else {
		flags = MAP_FIXED;
		if (f->usrv->priv)
			flags |= MAP_PRIVATE;
		else
			flags |= MAP_SHARED;

		prot = PROT_READ | PROT_WRITE;
		if (size) {
			p = mmap(dp, size, prot, flags, f->fd, 0);
			if (p == MAP_FAILED) {
				sp_uerror(errno);
				return -1;
			}

			f->dptr = p;
		}

		/* if the mapping is private, we'll set the dsize to Fmaxsize
		   so when the file grows it uses the anonymous dpad mapping */
		if (f->usrv->priv) {
			if (!f->dptr)
				f->dptr = f->dpad;
			size = Fmaxsize;
		}

		f->dsize = size;
	}

	MFILE_SIZE(f->haligned) = (u64) f->stat.st_size;
	MFILE_DPTR(f->haligned) = (u64) f->dptr;
	MFILE_DSIZE(f->haligned) = f->dsize;

	return 0;
}

static int
ufile_remove(Ufile *file)
{
	file->removed = 1;
	if (file->prev)
		file->prev->next = file->next;
	else
		file->usrv->files = file->next;

	if (file->next)
		file->next->prev = file->prev;

	file->prev = NULL;
	file->next = NULL;

	if (file->usrv->priv)
		ufile_decref(file);

	return 0;
}

static int
ufile_stat(Ufile *f)
{
	if (stat(f->path, &f->stat) < 0) {
		sp_uerror(errno);
		return -1;
	}

	if (S_ISDIR(f->stat.st_mode))
		f->stat.st_size = 0;

	return 0;
}

static Ufid *
ufid_alloc() 
{
	Ufid *f;

	f = sp_malloc(sizeof(*f));
	if (!f)
		return NULL;

	f->file = NULL;
	f->omode = -1;
	f->fd = -1;
	f->dir = NULL;
	f->diroffset = 0;
	f->direntname = NULL;

	return f;
}

static void
ufid_destroy(Ufid *f)
{
	ufile_decref(f->file);
	if (f->fd != -1)
		close(f->fd);

	if (f->dir)
		closedir(f->dir);

	free(f);
}

static int
ufid_stat(Ufid *fid)
{
	return ufile_stat(fid->file);
}

static void
ufs_fiddestroy(Spfid *fid)
{
	Ufid *f;

	f = fid->aux;
	ufid_destroy(f);
}

static int
omode2uflags(u8 mode)
{
	int ret;

	ret = 0;
	switch (mode & 3) {
	case Oread:
		ret = O_RDONLY;
		break;

	case Ordwr:
		ret = O_RDWR;
		break;

	case Owrite:
		ret = O_WRONLY;
		break;

	case Oexec:
		ret = O_RDONLY;
		break;
	}

	if (mode & Otrunc)
		ret |= O_TRUNC;

	if (mode & Oappend)
		ret |= O_APPEND;

	if (mode & Oexcl)
		ret |= O_EXCL;

	return ret;
}

static void
ustat2qid(struct stat *st, Spqid *qid)
{
	int n;

	qid->path = 0;
	n = sizeof(qid->path);
	if (n > sizeof(st->st_ino))
		n = sizeof(st->st_ino);
	memmove(&qid->path, &st->st_ino, n);
	qid->version = st->st_mtime ^ (st->st_size << 8);
	qid->type = ustat2qidtype(st);
}

static void
ufile2qid(Ufile *f, Spqid *qid)
{
	ustat2qid(&f->stat, qid);
	if (qid->type & Qtmem)
		qid->path = (u64) f->haligned;
}

static u8
ustat2qidtype(struct stat *st)
{
	u8 ret;

	ret = 0;
	if (S_ISDIR(st->st_mode))
		ret |= Qtdir;

	if (S_ISLNK(st->st_mode))
		ret |= Qtsymlink;

	if (S_ISREG(st->st_mode))
		ret |= Qtmem;

	return ret;
}

static u32
umode2npmode(mode_t umode)
{
	u32 ret;

	ret = umode & 0777;
	if (S_ISDIR(umode))
		ret |= Dmdir;

	if (S_ISREG(umode))
		ret |= Dmmem;

	return ret;
}

static mode_t
np2umode(u32 mode)
{
	mode_t ret;

	ret = mode & 0777;
	if (mode & Dmdir)
		ret |= S_IFDIR;

	if (!(ret&~0777))
		ret |= S_IFREG;

	if (mode & Dmsetuid)
		ret |= S_ISUID;
	if (mode & Dmsetgid)
		ret |= S_ISGID;

	return ret;
}

static mode_t
npstat2umode(Spstat *st)
{
	return np2umode(st->mode);
}

static void
ustat2npwstat(char *path, struct stat *st, Spwstat *wstat)
{
	Spuser *u;
	Spgroup *g;
	char *s;

	memset(wstat, 0, sizeof(*wstat));
	ustat2qid(st, &wstat->qid);
	wstat->mode = umode2npmode(st->st_mode);
	wstat->atime = st->st_atime;
	wstat->mtime = st->st_mtime;
	wstat->length = st->st_size;

	u = sp_uid2user(st->st_uid);
	g = sp_gid2group(st->st_gid);
	
	wstat->uid = u?u->uname:"???";
	wstat->gid = g?g->gname:"???";
	wstat->muid = "";

	wstat->extension = NULL;
	s = strrchr(path, '/');
	if (s)
		wstat->name = s + 1;
	else
		wstat->name = path;
}

static Spfcall*
ufs_attach(Spfid *nfid, Spfid *nafid, Spstr *uname, Spstr *aname)
{
	Spfcall* ret;
	Ufid *fid;
	Spqid qid;
	char *user;
	Usrv *usrv;

	user = NULL;
	ret = NULL;

	if (nafid != NULL) {
		sp_werror(Enoauth, EIO);
		goto done;
	}

	nfid->user = NULL;
	if (uname->len) {
		user = sp_strdup(uname);
		nfid->user = sp_uname2user(user);
		free(user);
	} 

	if (!nfid->user) {
		free(fid);
		sp_werror(Eunknownuser, EIO);
		goto done;
	}

	usrv = nfid->conn->srv->treeaux;
	fid = ufid_alloc();
	if (aname->len==0 || *aname->str!='/')
		fid->file = ufile_walk(usrv, NULL, "", 0);
	else
		fid->file = ufile_walk(usrv, NULL, aname->str, aname->len);

	if (!fid->file)
		goto done;

	nfid->aux = fid;
	ufile2qid(fid->file, &qid);
	ret = sp_create_rattach(&qid);
	sp_fid_incref(nfid);

done:
	return ret;
}

static int
ufs_clone(Spfid *fid, Spfid *newfid)
{
	Ufid *f, *nf;

	f = fid->aux;
	nf = ufid_alloc();
	ufile_incref(f->file);
	nf->file = f->file;
	newfid->aux = nf;

	return 1;	
}

static int
ufs_walk(Spfid *fid, Spstr* wname, Spqid *wqid)
{
	Ufid *f;
	Ufile *file;

	f = fid->aux;
	if (ufid_stat(f) < 0)
		return 0;

//	fprintf(stderr, "ufs_walk %.*s\n", wname->len, wname->str);
	file = ufile_walk(f->file->usrv, f->file, wname->str, wname->len);
	if (!file)
		return 0;

//	fprintf(stderr, "\tufs_walk so good \n");
	ufile_decref(f->file);
//	fprintf(stderr, "\tufs_walk so far \n");
	f->file = file;
	ufile2qid(f->file, wqid);

//	fprintf(stderr, "\tufs_walk done\n");
	return 1;
}

static Spfcall*
ufs_open(Spfid *fid, u8 mode)
{
	Ufid *f;
	Ufile *file;
	Spqid qid;

	f = fid->aux;
	if (ufid_stat(f) < 0)
		return NULL;

	file = f->file;
	if (S_ISDIR(file->stat.st_mode)) {
		f->dir = opendir(file->path);
		if (!f->dir) {
			sp_uerror(errno);
			goto error;
		}
		f->omode = mode;
	} else {
		f->fd = open(file->path, omode2uflags(mode));
		if (file->fd < 0) {
			sp_uerror(errno);
			goto error;
		}

//		if (mode & Otrunc)
			ufile_mmap(f->file);

		f->omode = mode;
	}

	if (ufid_stat(f) < 0)
		goto error;

	ufile2qid(f->file, &qid);
	return sp_create_ropen(&qid, 0);

error:
	return NULL;
}

static Spfcall*
ufs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension)
{
	int n, omode;
	Ufid *f;
	Ufile *file;
	Spfcall *ret;
	Spqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	omode = mode;
	f = fid->aux;
	file = f->file;
	if (ufid_stat(f) < 0)
		return NULL;

	n = strlen(file->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, file->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (stat(npath, &st)==0 || errno!=ENOENT) {
		sp_werror(Eexist, EEXIST);
		goto out;
	}

	if (perm & Dmdir) {
		if (mkdir(npath, perm & 0777) < 0) {
			sp_uerror(errno);
			goto out;
		}

		if (lstat(npath, &st) < 0) {
			sp_uerror(errno);
			rmdir(npath);
			goto out;
		}
		
		f->dir = opendir(npath);
		if (!f->dir) {
			sp_uerror(errno);
			remove(npath);
			goto out;
		}
	} else {
		f->fd = open(npath, O_CREAT|omode2uflags(mode), perm & 0777);
		if (f->fd < 0) {
			sp_uerror(errno);
			goto out;
		}

		if (stat(npath, &st) < 0) {
			sp_uerror(errno);
			remove(npath);
			goto out;
		}
	}

	file = ufile_walk(file->usrv, file, name->str, name->len);
	if (!file)
		goto out;

	ufile_decref(f->file);
	f->file = file;
	ufile2qid(f->file, &qid);
	ret = sp_create_rcreate(&qid, 0);

out:
	free(npath);
	return ret;
}

static u32
ufs_read_dir(Ufid *f, u8* buf, u64 offset, u32 count)
{
	int i, n, plen;
	char *dname, *path;
	struct dirent *dirent;
	struct stat st;
	Spwstat wstat;

	if (offset == 0) {
		rewinddir(f->dir);
		f->diroffset = 0;
	}

	plen = strlen(f->file->path);
	n = 0;
	dirent = NULL;
	dname = f->direntname;
	while (n < count) {
		if (!dname) {
			dirent = readdir(f->dir);
			if (!dirent)
				break;

			if (strcmp(dirent->d_name, ".") == 0
			|| strcmp(dirent->d_name, "..") == 0)
				continue;

			dname = dirent->d_name;
		}

		path = malloc(plen + strlen(dname) + 2);
		sprintf(path, "%s/%s", f->file->path, dname);
		
		if (stat(path, &st) < 0) {
			free(path);
			sp_uerror(errno);
			return 0;
		}

		ustat2npwstat(path, &st, &wstat);
		i = sp_serialize_stat(&wstat, buf + n, count - n - 1, 0);
		free(wstat.extension);
		free(path);
		path = NULL;
		if (i==0)
			break;

		dname = NULL;
		n += i;
	}

	if (f->direntname) {
		free(f->direntname);
		f->direntname = NULL;
	}

	if (dirent)
		f->direntname = strdup(dirent->d_name);

	f->diroffset += n;
	return n;
}

static Spfcall*
ufs_read(Spfid *fid, u64 offset, u32 count, Spreq *req)
{
	int n;
	u64 length;
	Ufid *f;
	Spfcall *ret;

	f = fid->aux;
	ret = sp_alloc_rread(count);
	if (f->dir)
		n = ufs_read_dir(f, ret->data, offset, count);
	else {
		length = MFILE_SIZE(f->file->haligned);
		if (offset > length)
			n = 0;
		else if (offset+count > length)
			n = length - offset;
		else
			n = count;

		memmove(ret->data, f->file->dptr + offset, n);
	}

	if (sp_haserror()) {
		free(ret);
		ret = NULL;
	} else
		sp_set_rread_count(ret, n);

	return ret;
}

static Spfcall*
ufs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n;
	Ufid *f;

	f = fid->aux;
	if (offset+count > Fmaxsize) {
		sp_werror("file too big", EIO);
		return NULL;
	}

	if (offset+count < f->file->dsize) {
		memmove(f->file->dptr + offset, data, count);
		n = count;
	} else {
		if (f->file->dptr)
			msync(f->file->dptr, f->file->dsize, MS_SYNC | MS_INVALIDATE);

		n = pwrite(f->fd, data, count, offset);
		if (n < 0)
			sp_uerror(errno);

		if (ufid_stat(f) < 0)
			return NULL;

		ufile_mmap(f->file);
	}

	return sp_create_rwrite(count);
}

static Spfcall*
ufs_clunk(Spfid *fid)
{
	Ufid *f;
	Spfcall *ret;

	f = fid->aux;
	ret = sp_create_rclunk();
	return ret;
}

static Spfcall*
ufs_remove(Spfid *fid)
{
	Ufid *f;
	Spfcall *ret;

	ret = NULL;
	f = fid->aux;
	if (remove(f->file->path) < 0) {
		sp_uerror(errno);
		goto out;
	}

	ufile_remove(f->file);
	ret = sp_create_rremove();

out:
	return ret;

}

static Spfcall*
ufs_stat(Spfid *fid)
{
	Ufid *f;
	Spfcall *ret;
	Spwstat wstat;

	f = fid->aux;
	if (ufid_stat(f) < 0)
		return NULL;

	ustat2npwstat(f->file->path, &f->file->stat, &wstat);
	ret = sp_create_rstat(&wstat, 0);
	free(wstat.extension);

	return ret;
}

static Spfcall*
ufs_wstat(Spfid *fid, Spstat *stat)
{
	Ufid *f;
	Spfcall *ret;
	uid_t uid;
	gid_t gid;
	char *npath, *p, *s;
	Spuser *user;
	Spgroup *group;
	struct utimbuf tb;

	ret = NULL;
	f = fid->aux;
	
	if (ufid_stat(f) < 0)
		return NULL;

	uid = (uid_t) -1;
	gid = (gid_t) -1;
	if (uid == -1 && stat->uid.len) {
		s = sp_strdup(&stat->uid);
		user = sp_uname2user(s);
		free(s);
		if (!user) {
			sp_werror(Eunknownuser, EIO);
			goto out;
		}

		uid = user->uid;
	}

	if (gid == -1 && stat->gid.len) {
		s = sp_strdup(&stat->gid);
		group = sp_gname2group(s);
		free(s);
		if (!group) {
			sp_werror(Eunknownuser, EIO);
			goto out;
		}

		gid = group->gid;
	}

	if (stat->mode != (u32)~0) {
		if (stat->mode&Dmdir && !S_ISDIR(f->file->stat.st_mode)) {
			sp_werror(Edirchange, EIO);
			goto out;
		}

		if (chmod(f->file->path, npstat2umode(stat)) < 0) {
			sp_uerror(errno);
			goto out;
		}
	}

	if (stat->mtime != (u32)~0) {
		tb.actime = 0;
		tb.modtime = stat->mtime;
		if (utime(f->file->path, &tb) < 0) {
			sp_uerror(errno);
			goto out;
		}
	}

	if (gid != -1) {
		if (chown(f->file->path, uid, gid) < 0) {
			sp_uerror(errno);
			goto out;
		}
	}

	if (stat->name.len != 0) {
		p = strrchr(f->file->path, '/');
		if (!p)
			p = f->file->path + strlen(f->file->path);

		npath = malloc(stat->name.len + (p - f->file->path) + 2);
		memmove(npath, f->file->path, p - f->file->path);
		npath[p - f->file->path] = '/';
		memmove(npath + (p - f->file->path) + 1, stat->name.str, stat->name.len);
		npath[(p - f->file->path) + 1 + stat->name.len] = 0;
		if (strcmp(npath, f->file->path) != 0) {
			if (rename(f->file->path, npath) < 0) {
				sp_uerror(errno);
				goto out;
			}

			free(f->file->path);
			f->file->path = npath;
		}
	}

	if (stat->length != ~0) {
		if (truncate(f->file->path, stat->length) < 0) {
			sp_uerror(errno);
			goto out;
		}
	}
	ret = sp_create_rwstat();
	
out:
	return ret;
}

