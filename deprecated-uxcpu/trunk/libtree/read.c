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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include "npfs.h"
#include "npclient.h"
#include "tree.h"

static mode_t
np2umode(u32 mode, Npstr *extension, int dotu)
{
	mode_t ret;

	ret = mode & 0777;
	if (mode & Dmdir)
		ret |= S_IFDIR;

	if (dotu) {
		if (mode & Dmsymlink)
			ret |= S_IFLNK;
		if (mode & Dmsocket)
			ret |= S_IFSOCK;
		if (mode & Dmnamedpipe)
			ret |= S_IFIFO;
		if (mode & Dmdevice) {
			if (extension && extension->str[0] == 'c')
				ret |= S_IFCHR;
			else
				ret |= S_IFBLK;
		}
	}

	if (!(ret&~0777))
		ret |= S_IFREG;

	if (mode & Dmsetuid)
		ret |= S_ISUID;
	if (mode & Dmsetgid)
		ret |= S_ISGID;

	return ret;
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

	return ret;
}

static void
ustat2qid(struct stat *st, Npqid *qid)
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

static u32
umode2npmode(mode_t umode, int dotu)
{
	u32 ret;

	ret = umode & 0777;
	if (S_ISDIR(umode))
		ret |= Dmdir;

	if (dotu) {
		if (S_ISLNK(umode))
			ret |= Dmsymlink;
		if (S_ISSOCK(umode))
			ret |= Dmsocket;
		if (S_ISFIFO(umode))
			ret |= Dmnamedpipe;
		if (S_ISBLK(umode))
			ret |= Dmdevice;
		if (S_ISCHR(umode))
			ret |= Dmdevice;
		if (umode & S_ISUID)
			ret |= Dmsetuid;
		if (umode & S_ISGID)
			ret |= Dmsetgid;
	}

	return ret;
}

static mode_t
npstat2umode(Npstat *st, int dotu)
{
	return np2umode(st->mode, &st->extension, dotu);
}

static Npwstat *
filestat(char *path, int dotu)
{
	int err;
	Npuser *u;
	Npgroup *g;
	char *s, ext[256], *sbuf;
	Npwstat *wstat;
	struct stat st;

	if (stat(path, &st) < 0) {
		np_werror("stat failed", errno);
		return NULL;
	}

	u = np_uid2user(st.st_uid);
	g = np_gid2group(st.st_gid);

	ext[0] = '\0';
	if (dotu) {
		if (wstat->mode & Dmsymlink) {
			err = readlink(path, ext, sizeof(ext) - 1);
			if (err < 0)
				err = 0;

			ext[err] = '\0';
		} else if (wstat->mode & Dmdevice) {
			snprintf(ext, sizeof(ext), "%c %u %u", 
				S_ISCHR(st.st_mode)?'c':'b',
				major(st.st_rdev), minor(st.st_rdev));
		} 
	}

	u = np_uid2user(st.st_uid);
	if (!u) {
		np_werror("cannot find user", EIO);
		return NULL;
	}

	g = np_gid2group(st.st_gid);
	if (!g) {
		np_werror("cannot find group", EIO);
		return NULL;
	}

	s = strrchr(path, '/');
	if (s)
		s++;
	else
		s = path;

	wstat = malloc(sizeof(*wstat) + strlen(s) + strlen(ext) + 2);
	if (!wstat) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	memset(wstat, 0, sizeof(*wstat));
	sbuf = (char *) wstat + sizeof(*wstat);

	ustat2qid(&st, &wstat->qid);
	wstat->mode = umode2npmode(st.st_mode, dotu);
	wstat->atime = st.st_atime;
	wstat->mtime = st.st_mtime;
	wstat->length = st.st_size;
	wstat->uid = u->uname;
	wstat->gid = g->gname;
	wstat->muid = "";
	wstat->n_uid = st.st_uid;
	wstat->n_gid = st.st_gid;

	wstat->name = sbuf;
	strcpy(wstat->name, s);
	sbuf += strlen(wstat->name) + 1;
	wstat->extension = sbuf;
	strcpy(wstat->extension, ext);
	sbuf += strlen(wstat->extension) + 1;

	return wstat;
}

Tfile *
readtree(char *file, int load)
{
	int i, n, fd;
	char *s;
	Tfile *ret, *f;
	Npwstat *ds;
	DIR *dir;
	struct dirent *de;

	ret = treealloc(file);
	if (!ret)
		return NULL;

	ret->de = filestat(file, 1);
	if (ret->de->mode & Dmdir) {
		dir = opendir(file);
		if (!dir) {
			np_werror("cannot open dir", errno);
			goto error;
		}

		while ((de = readdir(dir)) != NULL) {
			s = malloc(strlen(file) + strlen(de->d_name) + 2);
			if (!s) {
				np_werror(Enomem, ENOMEM);
				goto error;
			}

			sprintf(s, "%s/%s", file, de->d_name);
			f = readtree(s, load);
			if (!f) {
				free(s);
				free(ds);
				goto error;
			}

			f->next = ret->dir;
			ret->dir = f;
			free(s);
		}
		closedir(dir);
	} else if (load && ret->de->length) {
		fd = open(file, O_RDONLY);
		if (fd < 0) {
			np_werror("cannot open file", errno);
			goto error;
		}

		ret->data = malloc(ret->de->length);
		if (ret->data) {
			n = 0;
			while ((i = read(fd, ret->data + n, ret->de->length - n)) > 0)
				n += i;

			if (n < ret->de->length) {
				free(ret->data);
				ret->data = NULL;
			}
		}
	}

	close(fd);
	return ret;

error:
	if (fd >= 0)
		close(fd);

	treefree(ret);
	return NULL;
}

