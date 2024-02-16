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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/mman.h>
#include <time.h>
#include "spfs.h"
#include "yspufs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct File File;
typedef struct Fid Fid;

struct File {
	int		refcount;
	char*		name;
	u32		perm;
	u64*		length;
	u32		atime;
	u32*		mtime;
	Spuser*		uid;
	Spgroup*	gid;
	Spuser*		muid;
	char*		extension;
	Spqid		qid;
	int 		excl;
	u8		hna[256];	/* header, unaligned */

	File*		parent;
	File*		next;		/* siblings, protected by parent lock */
	File*		prev;

	File*		dirents;	/* if directory */
	File*		dirlast;

	u8*		header;		/* header contains length and mtime */
	u8*		data;
	u64		datasize;
};

struct Fid {
	File*	file;
	int	omode;
	u64	diroffset;
	File*	dirent;
};

static File *root;

//static char *E = "";

static Spfcall* ramfs_attach(Spfid *fid, Spfid *afid, Spstr *uname, Spstr *aname);
static int ramfs_clone(Spfid *fid, Spfid *newfid);
static int ramfs_walk(Spfid *fid, Spstr* wname, Spqid *wqid);
static Spfcall* ramfs_open(Spfid *fid, u8 mode);
static Spfcall* ramfs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, 
	Spstr *extension);
static Spfcall* ramfs_read(Spfid *fid, u64 offset, u32 count, Spreq *);
static Spfcall* ramfs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *);
static Spfcall* ramfs_clunk(Spfid *fid);
static Spfcall* ramfs_remove(Spfid *fid);
static Spfcall* ramfs_stat(Spfid *fid);
static Spfcall* ramfs_wstat(Spfid *fid, Spstat *stat);
static void ramfs_fiddestroy(Spfid *fid);

static void
file_incref0(File *f)
{
	f->refcount++;
}

static void
file_incref(File *f)
{
	if (!f)
		return;

	file_incref0(f);
}

static int
file_decref(File *f)
{
	int ret;

	if (!f)
		return 0;

	ret = --f->refcount;
	if (!ret) {
		assert(f->dirents == NULL);
		free(f->name);
		free(f->extension);
		munmap(f->data, f->datasize);
		free(f);
	} 

	return ret;
}

static File *
find_file(File *dir, char *name)
{
	File *ret;
	File *f;

	if (strcmp(name, "..") == 0)
		return dir->parent;

	ret = NULL;
	for(f = dir->dirents; f != NULL; f = f->next)
		if (strcmp(name, f->name) == 0) {
			ret = f;
			break;
		}

	return ret;
}

static int
check_perm(File *dir, Spuser *user, int perm)
{
	int n;

	if (!user)
		return 0;

	if (!perm)
		return 1;

	n = dir->perm & 7;
	if (dir->uid == user) {
		n |= (dir->perm >> 6) & 7;
		n |= (dir->perm >> 3) & 7;
	}

	if (!(n & perm)) {
		sp_werror(Eperm, EPERM);
		return 0;
	}

	return 1;
}

static File*
file_create(File *parent, char *name, int perm, Spuser *user)
{
	u64 m;
	File *file;

	file = sp_malloc(sizeof(*file));
	if (!file)
		return NULL;

	file->datasize = (u64) 1 * 1024 * 1024 * 1024;
	m = (u64) &file->hna[0];
	m += m%128?128-m%128:0;
	file->header = (u8 *) m;
	file->data = mmap(0, file->datasize, PROT_READ | PROT_WRITE, MAP_PRIVATE 
		| MAP_ANON | MAP_NORESERVE, -1, 0);
	if (file->data == MAP_FAILED) {
		sp_uerror(errno);
		free(file);
		return NULL;
	}

	MFILE_DPTR(file->header) = (u64) file->data;
	MFILE_DSIZE(file->header) = file->datasize;
	MFILE_SIZE(file->header) = 0;
	file->length = &MFILE_SIZE(file->header);
	file->mtime = &MFILE_MTIME(file->header);

	file->refcount = 0;
	file->name = name;
	file->perm = perm | Dmmem;
	file->atime = time(NULL);
	*file->mtime = time(NULL);
	file->uid = user;
	file->gid = user->dfltgroup;
	file->muid = user;
	file->extension = NULL;
	file->excl = 0;

	file->parent = parent;
	file->next = NULL;
	file->prev = NULL;
	file->dirents = NULL;
	file->dirlast = NULL;
	file->qid.type = file->perm >> 24;
	file->qid.version = 0;
	file->qid.path = (u64) file->header;

	return file;
}

static void
file2wstat(File *f, Spwstat *wstat)
{
	wstat->size = 0;
	wstat->type = 0;
	wstat->dev = 0;
	wstat->qid = f->qid;
	wstat->mode = f->perm;
	wstat->atime = f->atime;
	wstat->mtime = *f->mtime;
	wstat->length = *f->length;
	wstat->name = f->name;
	wstat->uid = f->uid->uname;
	wstat->gid = f->gid->gname;
	wstat->muid = f->muid->uname;
	wstat->extension = f->extension;
	wstat->n_uid = f->uid->uid;
	wstat->n_gid = f->gid->gid;
	wstat->n_muid = f->muid->uid;
}

static Fid*
fidalloc() {
	Fid *f;

	f = malloc(sizeof(*f));
	f->file = NULL;
	f->omode = -1;
	f->diroffset = 0;
	f->dirent = NULL;

	return f;
}

static void
ramfs_connclose(Spconn *conn)
{
	exit(0);
}

static void
ramfs_fiddestroy(Spfid *fid)
{
	Fid *f;

	f = fid->aux;
	if (!f)
		return;

	file_decref(f->file);
	file_decref(f->dirent);
	free(f);
}

static Spfcall*
ramfs_attach(Spfid *nfid, Spfid *nafid, Spstr *uname, Spstr *aname)
{
	Spfcall* ret;
	Fid *fid;
	char *u;
	Spuser *user;

	user = NULL;
	ret = NULL;

	if (nafid != NULL) {
		sp_werror(Enoauth, EIO);
		return NULL;
	}

	u = sp_strdup(uname);
	user = sp_uid2user(geteuid());
	free(u);
	if (!user) {
		sp_werror(Eunknownuser, EIO);
		return NULL;
	}

	if (!check_perm(root, user, 4)) 
		goto done;

	nfid->user = user;

	fid = fidalloc();
	fid->omode = -1;
	fid->omode = -1;
	fid->file = root;
	file_incref(root);
	nfid->aux = fid;
	sp_fid_incref(nfid);

	ret = sp_create_rattach(&fid->file->qid);
done:
	return ret;
}

static int
ramfs_clone(Spfid *fid, Spfid *newfid)
{
	Fid *f, *nf;

	f = fid->aux;
	nf = fidalloc();
	nf->file = f->file;
	file_incref(f->file);
	newfid->aux = nf;

	return 1;
}

static int
ramfs_walk(Spfid *fid, Spstr* wname, Spqid *wqid)
{
	char *name;
	Fid *f;
	File *file, *nfile;

	f = fid->aux;
	file = f->file;

	if (!check_perm(file, fid->user, 1))
		return 0;

	name = sp_strdup(wname);
	nfile = find_file(file, name);
	free(name);
	if (!nfile) {
		sp_werror(Enotfound, ENOENT);
		return 0;
	}

	file_incref(nfile);
	file_decref(file);
	f->file = nfile;

	*wqid = nfile->qid;
	return 1;
}

static Spfcall*
ramfs_open(Spfid *fid, u8 mode)
{
	int m;
	Fid *f;
	File *file;
	Spfcall *ret;

	ret = NULL;
	f = fid->aux;
	m = 0;
	switch (mode & 3) {
	case Oread:
		m = 4;
		break;

	case Owrite:
		m = 2;
		break;

	case Ordwr:
		m = 6;
		break;

	case Oexec:
		m = 1;
		break;
	}

	if (mode & Otrunc)
		m |= 2;

	file = f->file;
	if (!check_perm(file, fid->user, m))
		goto done;

	if (file->perm & Dmdir) {
		f->diroffset = 0;
		f->dirent = file->dirents;
		file_incref(f->dirent);
	} else if (mode & Otrunc)
		*file->length = 0;

	if (mode & Oexcl) {
		if (file->excl) {
			sp_werror(Eopen, EPERM);
			goto done;
		}
		file->excl = 1;
	}

	f->omode = mode;
	ret = sp_create_ropen(&file->qid, 0);

done:
	return ret;
}

static Spfcall*
ramfs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension)
{
	int m;
	Fid *f;
	File *dir, *file, *nf;
	char *sname;

	sname = NULL;
	file = NULL;

	f = fid->aux;
	dir = f->file;
	sname = sp_strdup(name);
	nf = find_file(dir, sname);
	if (nf) {
		sp_werror(Eexist, EEXIST);
		goto error;
	}

	if (!strcmp(sname, ".") || !strcmp(sname, "..")) {
		sp_werror(Eexist, EEXIST);
		goto error;
	}

	if (!check_perm(dir, fid->user, 2))
		goto error;

	if (perm & Dmdir)
		perm &= ~0777 | (dir->perm & 0777);
	else 
		perm &= ~0666 | (dir->perm & 0666);

	file = file_create(dir, sname, perm, dir->uid);
	file_incref(file);
	if (dir->dirlast) {
		dir->dirlast->next = file;
		file->prev = dir->dirlast;
	} else
		dir->dirents = file;

	dir->dirlast = file;
	dir->muid = fid->user;
	*dir->mtime = time(NULL);
	dir->qid.version++;

	/* we have to decref the dir because we remove it from the fid,
	   then we have to incref it because it has a new child file,
	   let's just skip playing with the ref */
	f->file = file;
	f->omode = mode;
	file_incref(file);

	if (perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice|Dmsocket)) {
		if (!fid->conn->dotu) {
			sp_werror(Eperm, EPERM);
			goto error;
		}

		file->extension = sp_strdup(extension);
	} else {
		m = 0;
		switch (mode & 3) {
		case Oread:
			m = 4;
			break;

		case Owrite:
			m = 2;
			break;

		case Ordwr:
			m = 6;
			break;

		case Oexec:
			m = 1;
			break;
		}

		if (mode & Otrunc)
			m |= 2;

		if (!check_perm(file, fid->user, m)) {
			file_decref(file);
			goto error;
		}

		if (mode & Oexcl)
			file->excl = 1;

		if (file->perm & Dmdir) {
			f->diroffset = 0;
			f->dirent = file->dirents;
			file_incref(f->dirent);
		}
	}

	return sp_create_rcreate(&file->qid, 0);

error:
	return NULL;
}

static Spfcall*
ramfs_read(Spfid *fid, u64 offset, u32 count, Spreq *req)
{
	int i, n;
	Fid *f;
	File *file, *cf;
	Spfcall *ret;
	u8* buf;
	Spwstat wstat;

	f = fid->aux;
	buf = malloc(count);
	file = f->file;
	if (file->perm & Dmdir) {
		if (offset == 0 && f->diroffset != 0) {
			file_decref(f->dirent);
			f->dirent = file->dirents;
			file_incref(f->dirent);
			f->diroffset = 0;
		}

		n = 0;
		cf = f->dirent;
		for(n = 0, cf = f->dirent; n<count && cf != NULL; cf = cf->next) {
			file2wstat(cf, &wstat);
			i = sp_serialize_stat(&wstat, buf + n, count - n - 1,
				fid->conn->dotu);

			if (i==0)
				break;

			n += i;
		}

		f->diroffset += n;
		file_incref(cf);
		file_decref(f->dirent);
		f->dirent = cf;
	} else {
		n = count;
		if (*file->length < offset+count)
			n = *file->length - offset;

		if (n < 0)
			n = 0;

		memmove(buf, file->data + offset, n);
	}

	file->atime = time(NULL);

	ret = sp_create_rread(n, buf);
	free(buf);
	return ret;
}

static Spfcall*
ramfs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	u64 len;
	Fid *f;
	File *file;

	f = fid->aux;
	file = f->file;
	if (f->omode & Oappend)
		offset = *file->length;

	len = *file->length;
	if (len < offset+count) {
		if (offset+count > file->datasize) {
			if (file->datasize - offset > 0)
				count = file->datasize - offset;
			else
				count = 0;
		}

		if (count) {
			if (len < offset)
				memset(file->data + len, 0, offset - len);

			*file->length = offset + count;
		}
	}

	if (count)
		memmove(file->data + offset, data, count);

	*file->mtime = time(NULL);
	file->atime = time(NULL);
	file->qid.version++;
	file->muid = fid->user;
	return sp_create_rwrite(count);
}

static Spfcall*
ramfs_clunk(Spfid *fid)
{
	return sp_create_rclunk();
}

static Spfcall*
ramfs_remove(Spfid *fid)
{
	Fid *f;
	File *file;
	Spfcall *ret;

	ret = NULL;
	f = fid->aux;
	file = f->file;
	if (file->perm&Dmdir && file->dirents) {
		sp_werror(Enotempty, EIO);
		return NULL;
	}

	if (!check_perm(file->parent, fid->user, 2))
		goto done;

	if (file->parent->dirents == file)
		file->parent->dirents = file->next;
	else
		file->prev->next = file->next;

	if (file->next)
		file->next->prev = file->prev;

	if (file == file->parent->dirlast)
		file->parent->dirlast = file->prev;

	file->prev = NULL;
	file->next = NULL;

	file->parent->muid = fid->user;
	*file->parent->mtime = time(NULL);
	file->parent->qid.version++;

	file_decref(file);
	file_decref(file->parent);
	ret = sp_create_rremove();
	sp_fid_decref(fid);

done:
	return ret;

}

static Spfcall*
ramfs_stat(Spfid *fid)
{
	Fid *f;
	Spwstat wstat;

	f = fid->aux;
	file2wstat(f->file, &wstat);
	return sp_create_rstat(&wstat, fid->conn->dotu);
}

static Spfcall*
ramfs_wstat(Spfid *fid, Spstat *stat)
{
	Fid *f;
	File *file, *nf;
	Spfcall *ret;
	char *sname, *oldname;
	u64 length, oldlength;
	u32 oldperm;
	u32 oldmtime;

	ret = NULL;
	oldlength = ~0;
	oldperm = ~0;
	oldmtime = ~0;
	oldname = NULL;

	f = fid->aux;
	file = f->file;
	if (file->perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice) && fid->conn->dotu) {
		sp_werror(Eperm, EPERM);
		goto out;
	}

	oldname = NULL;
	if (stat->name.len != 0) {
		if (!check_perm(file->parent, fid->user, 2))
			goto out;

		sname = sp_strdup(&stat->name);
		nf = find_file(file->parent, sname);

		if (nf) {
			free(sname);
			sp_werror(Eexist, EEXIST);
			goto out;
		}

		oldname = file->name;
		file->name = sname;
	}

	if (stat->length != (u64)~0) {
		if (!check_perm(file, fid->user, 2) || file->perm&Dmdir)
			goto out;

		oldlength = *file->length;
		length = stat->length;
		if (*file->length < length)
			memset(file->data+*file->length, 0, length-*file->length);

		*file->length = length;
	}

	if (stat->mode != (u32)~0) {
		if (file->uid != fid->user) {
			sp_werror(Eperm, EPERM);
			goto out;
		}

		oldperm = file->perm;
		file->perm = stat->mode;
	}

	if (stat->mtime != (u32)~0) {
		if (file->uid != fid->user) {
			sp_werror(Eperm, EPERM);
			goto out;
		}

		oldmtime = *file->mtime;
		*file->mtime = stat->mtime;
	}

	ret = sp_create_rwstat();
	
out:
	if (sp_haserror()) {
		if (oldname) {
			free(file->name);
			file->name = oldname;
		}

		if (oldperm != ~0)
			file->perm = oldperm;

		if (oldmtime != ~0)
			*file->mtime = oldmtime;

		if (oldlength != ~0)
			*file->length = oldlength;
	} else 
		free(oldname);

	return ret;
}

int
ramfs_init()
{
	Spsrv *srv;
	Spuser *user;

	srv = sp_srv_create();
	if (!srv)
		return -1;

	user = sp_uid2user(geteuid());
	root = file_create(NULL, strdup(""), ROOTPERM | Dmdir, user);
	file_incref(root);
	root->parent = root;

	srv->dotu = 0;
	srv->debuglevel = 0;
	srv->connclose = ramfs_connclose;
	srv->attach = ramfs_attach;
	srv->clone = ramfs_clone;
	srv->walk = ramfs_walk;
	srv->open = ramfs_open;
	srv->create = ramfs_create;
	srv->read = ramfs_read;
	srv->write = ramfs_write;
	srv->clunk = ramfs_clunk;
	srv->remove = ramfs_remove;
	srv->stat = ramfs_stat;
	srv->wstat = ramfs_wstat;
	srv->fiddestroy = ramfs_fiddestroy;

	return register_dev('r', srv);
}

