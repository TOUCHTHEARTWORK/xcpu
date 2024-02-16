#include <stdlib.h>
#include <stdio.h>
/* what a kludge! see man 2 pread */
#define _XOPEN_SOURCE 500
/* and yuck it doesn't work anyway. */
#include <unistd.h>
#include <string.h>
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
#include <time.h>
#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "ixvm.h"

static Spfile *dir_first(Spfile *);
static Spfile *dir_next(Spfile *, Spfile *);
static void session_destroy(Spfile *);
static void session_remove_dir(Xsession *xs);
static int session_wstat(Spfile *, Spstat *);
static int clone_read(Spfilefid *, u64, u32, u8 *, Spreq *);
static int clone_openfid(Spfilefid *);
static void clone_closefid(Spfilefid *);
static int clone_wstat(Spfile *, Spstat *);
static int ctl_read(Spfilefid *, u64, u32, u8 *, Spreq *);
static int ctl_write(Spfilefid *f, u64 offset, u32 count, u8 *data, Spreq *req);
static int ctl_wstat(Spfile *, Spstat *);
static int filebuf_read(Spfilefid *, u64, u32, u8 *, Spreq *);
static int filebuf_write(Spfilefid *, u64, u32, u8 *, Spreq *);
static int filebuf_wstat(Spfile *, Spstat *);
static int id_read(Spfilefid *, u64, u32, u8 *, Spreq *);
static void reftrack_ref(Spfile *, Spfilefid *);
static void reftrack_unref(Spfile *, Spfilefid *);
static Spfile *create_file(Spfile *parent, char *name, u32 mode, u64 qpath, 
	void *ops, Spuser *usr, void *aux);
//static int ignore_wstat(Spfile *, Spstat *);
static int info_read(Spfilefid *, u64, u32, u8 *, Spreq *);

static int xclone(Spfid *fid, Spfid *newfid);
static int xwalk(Spfid *fid, Spstr *wname, Spqid *wqid);
static Spfcall *xopen(Spfid *fid, u8 mode);
static Spfcall *xcreate(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension);
static Spfcall* xread(Spfid *fid, u64 offset, u32 count, Spreq *);
static Spfcall* xwrite(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *);
static Spfcall* xclunk(Spfid *fid);
static Spfcall* xremove(Spfid *fid);
static Spfcall* xstat(Spfid *fid);
static Spfcall* xwstat(Spfid *fid, Spstat *stat);
static void xfiddestroy(Spfid *fid);

static void sigchld_notify(Spfd *spfd, void *aux);
static void sigchld_handler(int sig);

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Spdirops session_ops = {
	.first = dir_first,
	.next = dir_next,
	.wstat = session_wstat,
	.destroy = session_destroy,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Spfileops clone_ops = {
	.read = clone_read,
	.openfid = clone_openfid,
	.closefid = clone_closefid,
	.wstat = clone_wstat,
};

Spfileops ctl_ops = {
	.read = ctl_read,
	.write = ctl_write,
	.wstat = ctl_wstat,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Spfileops id_ops = {
	.read = id_read,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Spfileops fbuf_ops = {
	.read = filebuf_read,
	.write = filebuf_write,
	.wstat = filebuf_wstat,
};

Spfileops info_ops = {
	.read = info_read,
};

extern int spc_chatty;

static int session_next_id;
static Spfile *root;
static Spuser *user;
static Xfilebuf archbuf;
static char *tmppath = "/tmp";
static int chld_fd;
static int defaulttype = TypeNone;

static int debuglevel;
Xsession *sessions;
int sameuser;
Spsrv *srv;

void
change_user(Spuser *user)
{
	if (!sameuser)
		sp_change_user(user);
}

/*static void
bufinit(Xfilebuf *fbuf)
{
	fbuf->size = 0;
	fbuf->buf = NULL;
}*/

static int
bufread(Xfilebuf *fbuf, u64 offset, u32 count, u8 *data)
{
	if (offset+count > fbuf->size)
		count = fbuf->size - offset;
 
	if (count < 0)
		count = 0;

	memmove(data, fbuf->buf + offset, count);
	return count;
}

static int
bufwrite(Xfilebuf *fbuf, u64 offset, u32 count, void *data)
{
	char *tbuf;

	if (offset+count > fbuf->size) {
		tbuf = realloc(fbuf->buf, offset + count);
		if (tbuf) {
			fbuf->buf = tbuf;
			fbuf->size = offset + count;
		}
	}

	if (offset + count > fbuf->size)
		count = fbuf->size - offset;

	if (count < 0)
		count = 0;

	memmove(fbuf->buf + offset, data, count);

	return count;
}

static void
bufsetsize(Xfilebuf *fbuf, int size)
{
	if (fbuf->size < size)
		bufwrite(fbuf, size, 0, NULL);
	else
		fbuf->size = size;
}

/*static void
buffree(Xfilebuf *fbuf)
{
	fbuf->size = 0;
	free(fbuf->buf);
}*/

static int
xrmdir(char *dir)
{
	int n, dlen, namlen;
	DIR *d;
	struct dirent *de;
	char *buf;

	dlen = strlen(dir);
	d = opendir(dir);
	if (!d)
		return errno;

	n = 0;
	while (!n && (de = readdir(d)) != NULL) {\
		namlen = strlen(de->d_name);
		if (namlen==1 && *de->d_name=='.')
			continue;

		if (namlen==2 && !memcmp(de->d_name, "..", 2))
			continue;

		buf = malloc(dlen + namlen + 2);
		sprintf(buf, "%s/%s", dir, de->d_name);
		if (de->d_type == DT_DIR)
			n = xrmdir(buf);
		else 
			if (unlink(buf) < 0)
				n = errno;
		free(buf);
	}
	closedir(d);

	if (!n && rmdir(dir)<0)
		n = errno;

	return n;
}

static int 
filebuf_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Xfilebuf *fbuf;

	fbuf = fid->file->aux;
	return bufread(fbuf, offset, count, data);
}

static int
filebuf_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Xfilebuf *fbuf;

	fbuf = fid->file->aux;
	return bufwrite(fbuf, offset, count, data);
}

/*static int
ignore_wstat(Spfile *f, Spstat *stat)
{
	return 1;
} */

static int
filebuf_wstat(Spfile *f, Spstat *stat)
{
	Xfilebuf *fbuf;

	fbuf = f->aux;
	if (f->length != stat->length)
		bufsetsize(fbuf, stat->length);

	return 1;
}

Xsession*
session_create(int msize)
{
	int n;
	char *buf;
	Xsession *s, *ps, *xs;

	buf = NULL;
	xs = sp_malloc(sizeof(*xs));
	if (!xs)
		return NULL;

	xs->refcount = 1;
	xs->state = Initializing;
	xs->mode = Normal;
	xs->gid = strdup("");
	xs->lid = 0;
	xs->ctl = NULL;
	xs->ctlreq = NULL;
	xs->ctlpos = 0;
	xs->pid = -1;
	xs->memsize = 128;
	xs->cfg = NULL;
	xs->monin = -1;
	xs->monout = -1;
	xs->bdevs = NULL;
	xs->ndevs = NULL;
	xs->vmimage = NULL;
	xs->sroot = NULL;
	xs->fsdir = NULL;
	xs->next = NULL;
	xs->type = defaulttype;

	n = strlen(tmppath) + 16;
	buf = sp_malloc(n);
	if (!buf) 
		goto error;

	sprintf(buf, "%s/ixvm-XXXXXX", tmppath);
	xs->dirpath = mkdtemp(buf);
	if (!xs->dirpath) {
		sp_uerror(errno);
		goto error;
	}

	xs->sid = session_next_id;
	session_next_id++;
	for(ps=NULL, s=sessions; s!=NULL; ps=s, s=s->next)
		if (s->sid >= xs->sid)
			break;

	if (s && s->sid == xs->sid) {
		xs->sid++;
		while (s!=NULL && xs->sid==s->sid) {
			xs->sid++;
			if (xs->sid == INT_MAX) {
				xs->sid = 0;
				s = sessions;
				ps = NULL;
			} else {
				ps = s;
				s = s->next;
			}
		}
	}

	if (ps) {
		xs->next = ps->next;
		ps->next = xs;
	} else {
		xs->next = sessions;
		sessions = xs;
	}

	return xs;

error:
	free(xs->gid);
	free(buf);
	free(xs);
	return NULL;
}

static void
session_destroy(Spfile *file)
{
	Xsession *xs, *s, *ps;
	Blockdev *bd, *bd1;
	Netdev *nd, *nd1;

	xs = file->aux;
	for(s=sessions, ps=NULL; s != NULL; ps=s, s=s->next)
		if (s == xs)
			break;

	if (ps)
		ps->next = xs->next;
	else
		sessions = xs->next;

	free(xs->gid);
	free(xs->ctl);

	if (xs->dirpath) {
		xrmdir(xs->dirpath);
		free(xs->dirpath);
	}

	if (xs->pid != -1)
		kill(xs->pid, SIGTERM);

	bd = xs->bdevs;
	while (bd != NULL) {
		bd1 = bd->next;
		free(bd->devname);
		free(bd->devimage);
		free(bd);
		bd = bd1;
	}

	nd = xs->ndevs;
	while (nd != NULL) {
		nd1 = nd->next;
		free(nd->id);
		free(nd->mac);
		free(nd);
		nd = nd1;
	}

	if (xs->monin >= 0)
		close(xs->monin);

	if (xs->monout >= 0)
		close(xs->monout);

	free(xs);
}

static int
session_add_dir(Xsession *xs, Spuser *user)
{
	u64 qpath;
	char buf[32];
	Spfile *sroot;

	snprintf(buf, sizeof buf, "%d", xs->sid);
	qpath = QPATH(xs->sid);
	sroot = create_file(root, buf, 0555 | Dmdir, qpath, &session_ops,
		user, xs);
	if (!sroot)
		return 0;

	xs->sroot = sroot;
	if (!create_file(sroot, "ctl", 0660, qpath | Qctl, &ctl_ops, NULL, xs))
		goto error;

	if (!create_file(sroot, "info", 0660, qpath | Qinfo, &info_ops, NULL, NULL))
		goto error;

	if (!create_file(sroot, "id", 0440, qpath | Qid, &id_ops, NULL, NULL))
		goto error;

	xs->fsdir = create_file(sroot, "fs", 0770, qpath | Qfs, NULL, NULL, xs);
	if (!xs->fsdir)
		goto error;

	return 1;

error:
	session_remove_dir(xs);
	return 0;
}

static void
session_remove_dir(Xsession *xs)
{
	Spfile *f, *f1, *sroot;

	sroot = xs->sroot;
	if (root->dirfirst == sroot)
		root->dirfirst = sroot->next;
	else
		sroot->prev->next = sroot->next;

	if (sroot->next)
		sroot->next->prev = sroot->prev;

	if (sroot == root->dirlast)
		root->dirlast = sroot->prev;

	sroot->prev = sroot->next = sroot->parent = NULL;

	// remove the children
	f = sroot->dirfirst;
	sroot->dirfirst = sroot->dirlast = NULL;
	while (f != NULL) {
		f1 = f->next;
		spfile_decref(f->parent);
		spfile_decref(f);
		f = f1;
	}
}

static void
session_wipe(Xsession *xs)
{
//	fprintf(stderr, "session_wipe %p\n", xs);
	if (xs->state == Wiped)
		return;

	xs->state = Wiped;
	session_remove_dir(xs);
	if (xs->pid != -1)
		kill(xs->pid, SIGTERM);
}

int
session_incref(Xsession *xs)
{
	int ret;

//	fprintf(stderr, "session_incref refcount %d\n", xs->refcount + 1);
	ret = ++xs->refcount;

	return ret;
}

void
session_decref(Xsession *xs)
{
	int wipe;
	Spfile *sroot;

//	fprintf(stderr, "session_decref refcount %d\n", xs->refcount - 1);
	sroot = xs->sroot;
	xs->refcount--;
	if (xs->refcount)
		return;

//	fprintf(stderr, "session_decref xs %p file refcount %d\n", xs, xs->file->refcount);
	wipe = !xs->refcount && xs->mode==Normal && xs->state != Running;
	if (wipe) 
		session_wipe(xs);

	spfile_decref(sroot);
}

static int
session_wstat(Spfile *f, Spstat *st)
{
	char *uname;
	Spuser *user;
	Spfile *c;

	uname = sp_strdup(&st->uid);
	if (!uname)
		return 0;

	user = sp_unix_users->uname2user(sp_unix_users, uname);
	free(uname);

	if (!user) {
		sp_werror("invalid user", EIO);
		return 0;
	}

	for(c = f->dirfirst; c != NULL; c = c->next) {
		c->uid = c->muid = user;
		c->gid = user->dfltgroup;
	}

	return 1;
}


static Spfile *
create_file(Spfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Spuser *usr, void *aux)
{
	Spfile *ret;

	ret = spfile_alloc(parent, name, mode, qpath, ops, aux);
	if (!ret)
		return NULL;

	if (parent) {
		if (parent->dirlast) {
			parent->dirlast->next = ret;
			ret->prev = parent->dirlast;
		} else
			parent->dirfirst = ret;

		parent->dirlast = ret;
		if (!usr)
			usr = parent->uid;
	}

	if (!usr)
		usr = user;

	ret->atime = ret->mtime = time(NULL);
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
	spfile_incref(ret);
	return ret;
}

static void
archinit()
{
	int n;
	struct utsname u;
	char *m, *buf;
	char *ppc = "powerpc";

	uname(&u);
	if (strncmp(u.machine, "Power", 5) == 0)
		m = ppc;
	else
		m = u.machine;

	n = strlen(m) + strlen(u.sysname) + 3;
	buf = malloc(n);
	snprintf(buf, n, "/%s/%s\n", u.sysname, m);

	bufwrite(&archbuf, 0, strlen(buf), (u8 *) buf);
	free(buf);
}

static void
fsinit()
{
	archinit();

	root = spfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	spfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;

	create_file(root, "clone", 0444, Qclone, &clone_ops, NULL, NULL);
	create_file(root, "arch", 0444, Qarch, &fbuf_ops, NULL, &archbuf);
}

static Spfile*
dir_first(Spfile *dir)
{
	spfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Spfile*
dir_next(Spfile *dir, Spfile *prevchild)
{
	spfile_incref(prevchild->next);
	return prevchild->next;
}


static int
clone_openfid(Spfilefid *fid)
{
	Xsession *xs;

	xs = session_create(fid->fid->conn->msize);
	if (!xs)
		return 0;

	if (!session_add_dir(xs, fid->fid->user))
		return 0;

	//fprintf(stderr, "clone_openfid sref %d\n", xs->file->refcount);
	fid->aux = xs->sroot;
	return 1;
}

static void
clone_closefid(Spfilefid *fid) {
	Spfile *session;
	Xsession *xs;

	session = fid->aux;
	if(!session)
		return;

	xs = session->aux;
	session_decref(xs);
}

static int
clone_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Xsession *xs;
	char buf[32];

	xs = ((Spfile *) fid->aux)->aux;
	snprintf(buf, sizeof buf, "%d", xs->sid);

	return cutstr(data, offset, count, buf, 0);
}

static int 
clone_wstat(Spfile *f, Spstat *st) {
	char *uname, *gname;
	Spuser *user;
	Spgroup *group;

	user = NULL;
	group = NULL;
	if (st->uid.len) {
		uname = sp_strdup(&st->uid);
		user = sp_unix_users->uname2user(sp_unix_users, uname);
		free(uname);
		if (!user) {
			sp_werror("invalid user", EIO);
			return 0;
		}
	}

	if (st->n_uid != ~0) {
		user = sp_unix_users->uid2user(sp_unix_users, st->n_uid);
		if (!user) {
			sp_werror("invalid user", EIO);
			return 0;
		}
	}

	if (user)
		f->uid = user;

	if (st->gid.len) {	
		gname = sp_strdup(&st->gid);
		group = sp_unix_users->gname2group(sp_unix_users, gname);
		free(gname);
		if (!group) {
			sp_werror("invalid group", EIO);
			return 0;
		}
	}

	if (st->n_gid != ~0) {
		group = sp_unix_users->gid2group(sp_unix_users, st->n_gid);
		if (!group) {
			sp_werror("invalid group", EIO);
			return 0;
		}
	}

	if (group)
		f->gid = group;

	if (st->mode != ~0)
		f->mode = st->mode & 0777;

	return 1;
}

int
session_set_type(Xsession *xs, char *cmd)
{
	int ret, nargs;
	char **toks;

	/* the only command servicable from here would be "type" */
	ret = 0;
	nargs = tokenize(cmd, &toks);
	if (nargs < 0) {
		sp_werror("invalid format", EIO);
		return 0;
	} else if(nargs == 2) {
		if(strncmp("type", toks[0], 4) == 0) {
			if(xs->type != TypeNone) {
				sp_werror("attempt to execute a type command for a VM with type", EIO);
			} else if(strncmp("xen", toks[1], 3) == 0) {
				xs->type = TypeXen;
				ret = 1;
			} else if(strncmp("qemu", toks[1], 4) == 0) {
				xs->type = TypeQemu;
				ret = 1;
			} else {
				sp_werror("invalid type", EIO);
			}
		} else {
			sp_werror("only 'type' command valid for a session with no type information", EIO);
		}
	} else {
		sp_werror("only 'type' command valid for a session with no type information", EIO);
	}

	free(toks);
	return ret;
}


/*static int
session_set_id(Xsession *xs, char *id)
{
	int lid;
	char *gid, *p, *t;

	gid = NULL;
	lid = -1;

	p = strchr(id, '/');
	if (p) {
		*p = '\0';
		p++;

		if (*p != '\0') {
			lid = strtol(p, &t, 10);
			if (*t != '\0') {
				sp_werror("syntax error", EIO);
				return 0;
			}

			if (lid < 0) {
				sp_werror("negative id not permitted", EIO);
				return 0;
			}
		}
	}

	if (*id != '\0') {
		free(xs->gid);
		xs->gid = strdup(id);
	}

	if (lid >= 0)
		xs->lid = lid;

	return 1;
}*/

static int
ctl_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Xsession *xs;
	char buf[16];

	xs = fid->file->parent->aux;
	snprintf(buf, sizeof buf, "%d", xs->pid);

	return cutstr(data, offset, count, buf, 0);
}

void
ctl_execute_commands(Xsession *xs)
{
	int n, ecode;
	char *p, *ename;
	Spfcall *fc;

	if (!xs->ctlreq)
		return;

	while ((p = strchr(xs->ctl + xs->ctlpos, '\n')) != NULL) {
		*p = '\0';
		if(xs->type == TypeQemu)
			n = execute_command_qemu(xs, xs->ctl + xs->ctlpos);
		else if(xs->type == TypeXen)
			n = execute_command_xen(xs, xs->ctl + xs->ctlpos);
		else 
			n = session_set_type(xs, xs->ctl + xs->ctlpos);

		xs->ctlpos = (p - xs->ctl) + 1;

		if (n < 0)
			return;
		if (!n) {
			sp_rerror(&ename, &ecode);
			if (ename == Enomem) 
				fc = sp_srv_get_enomem(xs->ctlreq->conn->srv, xs->ctlreq->conn->dotu);
			else
				fc = sp_create_rerror(ename, ecode, xs->ctlreq->conn->dotu);
			sp_werror(NULL, 0);
			goto done;
		}
	}

	if (xs->ctl[xs->ctlpos] == '\0') {
		free(xs->ctl);
		xs->ctl = NULL;
		xs->ctlpos = 0;
		n = xs->ctlreq->tcall->count;
	} else if (xs->ctlpos > 0) {
		memmove(xs->ctl, xs->ctl + xs->ctlpos, strlen(xs->ctl + xs->ctlpos) + 1);
		xs->ctlpos = 0;
		n = xs->ctlreq->tcall->count - strlen(xs->ctl);
		if (n < 0) 
			n = xs->ctlreq->tcall->count;
	}

	fc = sp_create_rwrite(n);

done:
	sp_respond(xs->ctlreq, fc);
	xs->ctlreq = NULL;
	xs->ctlpos = 0;
}

static int
ctl_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int len;
	char *s;
	Xsession *xs;

	xs = fid->file->parent->aux;
	if (xs->ctlreq) {
		sp_werror("cannot write while the session is cloning", EIO);
		return -1;
	}

	len = xs->ctl?strlen(xs->ctl):0;
	s = realloc(xs->ctl, len + count + 1);
	if (!s) {
		sp_werror(Enomem, ENOMEM);
		return -1;
	}

	xs->ctl = s;
	memmove(xs->ctl + len, data, count);
	xs->ctl[len + count] = '\0';

	xs->ctlreq = req;
	xs->ctlpos = 0;
	ctl_execute_commands(xs);

	return -1;
}

static int 
ctl_wstat(Spfile *f, Spstat *st) {
	Xsession *xs;

	xs = f->parent->aux;
	if (st->length == 0) {
		free(xs->ctl);
		xs->ctl = NULL;

		return 1;
	}

	sp_werror("unsupported operation", EPERM);
	return 0;
}

static int 
id_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n;
	char *buf;
	Xsession *xs;

	xs = fid->file->parent->aux;
	buf = sp_malloc(strlen(xs->gid) + 32);
	if (!buf)
		return -1;

	sprintf(buf, "%s/%d", xs->gid, xs->lid);
	n = cutstr(data, offset, count, buf, 0);
	free(buf);

	return n;
}

static int 
info_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	int n;
	char *buf;
	Xsession *xs;

	xs = fid->file->parent->aux;
	buf = sp_malloc(1000);
	if (!buf)
		return -1;

	sprintf(buf, "not implemented yet");
	n = cutstr(data, offset, count, buf, 0);
	free(buf);

	return n;
}

static void
reftrack_ref(Spfile *file, Spfilefid *fid)
{
	Xsession *xs;

	if ((file->qid.path&QMASK) == 0)
		xs = file->aux;
	else
		xs = file->parent->aux;

	//fprintf(stderr, "reftrack_ref %s %d sref %d\n", file->name, file->refcount, xs->file->refcount);
	//fprintf(stderr, "reftrack_ref %s sroot %p %d\n", file->name, xs->sroot, xs->sroot->refcount);
	session_incref(xs);
}

static void 
reftrack_unref(Spfile *file, Spfilefid *fid)
{
	Xsession *xs;

	if ((file->qid.path&QMASK) == 0)
		xs = file->aux;
	else
		xs = file->parent->aux;

	//fprintf(stderr, "reftrack_unref %s %d sref %d\n", file->name, file->refcount-1, xs->file->refcount);
	//fprintf(stderr, "reftrack_unref %s sroot %p %d\n", file->name, xs->sroot, xs->sroot->refcount);
	session_decref(xs);
}

void
usage()
{
	fprintf(stderr, "kvmfs: -h -d -s -m msize -p port -t tmpdir -k type\n");
	exit(-1);
}

static Spfcall *
xflush(Spreq *req)
{
	int n;
	Xsession *xs;
	Spfilefid *fid;
	Spfile *f;

	if (!req->fid || !req->fid->aux)
		return NULL;

	change_user(req->fid->user);
	fid = req->fid->aux;
	f = fid->file;
	xs = f->parent->aux;

	n = 0;
	switch (f->qid.path & 255) {
	case Qctl:
		n = 1;
		xs->ctlreq = NULL;	/* TODO: real flush */
		break;
	}

	if (n) {
		sp_respond(req, NULL);
		return sp_create_rflush();
	} else
		return NULL;
}

static int 
xclone(Spfid *fid, Spfid *newfid)
{
	int ret;
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		ret = spfile_clone(fid, newfid);
	else
		ret = ufs_clone(fid, newfid);

	return ret;
}

static int 
xwalk(Spfid *fid, Spstr *wname, Spqid *wqid)
{
	int ret;
	char *path;
	Spfilefid *f;
	Spfile *file;
	Xsession *xs;
	Fsfid *fsfid;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		ret = spfile_walk(fid, wname, wqid);
	else {
		fsfid = f->aux;
		xs = fsfid->xs;
		if (xs) {
			file = xs->fsdir;
			path = xs->dirpath;
		} else {
			sp_werror("internal error", EIO);
			return 0;
		}
 
		if (wname->len==2 && !memcmp(wname->str, "..", 2) 
		&& !strcmp(fsfid->path, path)) { 
			f->file = file;
			spfile_incref(f->file);
			if (fsfid->fd != -1)
				close(fsfid->fd);
			if (fsfid->dir)
				closedir(fsfid->dir);

			free(fsfid->path);
			free(fsfid);
			f->aux = NULL;
			ret = spfile_walk(fid, wname, wqid);
		} else
			ret = ufs_walk(fid, wname, wqid);
	}

	f = fid->aux;
	if (f->file) {
		xs = f->file->aux;
		if (!f->file->ops) {
			spfile_decref(f->file);
			ufs_attach(fid, xs, wqid);
		}
	} 

	return ret;
}

static Spfcall *
xopen(Spfid *fid, u8 mode)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_open(fid, mode);
	else
		return ufs_open(fid, mode);
}

static Spfcall *
xcreate(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_create(fid, name, perm, mode, extension);
	else
		return ufs_create(fid, name, perm, mode, extension);
}

static Spfcall *
xread(Spfid *fid, u64 offset, u32 count, Spreq *req)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_read(fid, offset, count, req);
	else
		return ufs_read(fid, offset, count, req);
}

static Spfcall *
xwrite(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_write(fid, offset, count, data, req);
	else
		return ufs_write(fid, offset, count, data, req);
}

static Spfcall*
xclunk(Spfid *fid)
{
	Spfilefid *f;
	Spfcall *ret;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		ret = spfile_clunk(fid);
	else
		ret = ufs_clunk(fid);

	return ret;
}

static Spfcall*
xremove(Spfid *fid)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_remove(fid);
	else
		return ufs_remove(fid);
}

static Spfcall*
xstat(Spfid *fid)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_stat(fid);
	else
		return ufs_stat(fid);
}

static Spfcall*
xwstat(Spfid *fid, Spstat *stat)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (f->file)
		return spfile_wstat(fid, stat);
	else
		return ufs_wstat(fid, stat);
}

static void 
xfiddestroy(Spfid *fid)
{
	Spfilefid *f;

	change_user(fid->user);
	f = fid->aux;
	if (!f)
		return;

	if (f->file)
		spfile_fiddestroy(fid);
	else
		ufs_fiddestroy(fid);
}

static void
sigchld_setup_pipe(void)
{
	int pip[2];

	if (pipe(pip) < 0) 
		return;

	fcntl(pip[0], F_SETFD, FD_CLOEXEC);
	fcntl(pip[1], F_SETFD, FD_CLOEXEC);
	spfd_add(pip[0], sigchld_notify, (int *)(long) pip[0]);
	chld_fd = pip[1];
}

static void
sigchld_notify(Spfd *spfd, void *aux)
{
	Xsession *s;
	int pid, status;

	if (!spfd_has_error(spfd))
		return;

//	fprintf(stderr, "sigchld_notify\n");
	close((long) aux);
	spfd_remove(spfd);
	sigchld_setup_pipe();

	while (1) {
		pid = waitpid(-1, &status, WNOHANG);

		if (pid <= 0)
			break;

//		fprintf(stderr, "chld_notify pid %d status %d\n", pid, status);
		for(s = sessions; s != NULL; s = s->next)
			if (s->pid == pid)
				break;

		if (!s)
			continue;

		if (s->state != Wiped)
			s->state = Finished;
		s->pid = -1;

		// TODO: do we need to give exit status back?
		session_decref(s);
	}
}

static void
sigchld_handler(int sig)
{
//	fprintf(stderr, "sigchld_handler\n");
	if (chld_fd >= 0) {
		close(chld_fd);
		chld_fd = -1;
	}
}

int 
main(int argc, char *argv[])
{
	int c, ecode;
	int port;
	int msize;
	pid_t pid;
	char *s, *ename;
	struct sigaction sact;

	msize = 8216;
	spc_chatty = 0;
	user = sp_unix_users->uid2user(sp_unix_users, geteuid());
	port = 7777;
	while ((c = getopt(argc, argv, "hdDsp:t:m:k:")) != -1) {
		switch (c) {
		case 'D':
			spc_chatty = 1;
			break;

		case 'd':
			debuglevel = 1;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 't':
			tmppath = strdup(optarg);
			break;

		case 's':
			sameuser++;
			break;

		case 'm':
			msize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			spc_msize = msize;
			break;

		case 'k':
			if(strncmp(optarg, "xen", 3) == 0)
				defaulttype = TypeXen;
			else if(strncmp(optarg, "qemu", 4) == 0)
				defaulttype = TypeQemu;
			else {
				fprintf(stderr, "unknown VM type: %s (use -h for help)\n", optarg);
				exit(2);
			}
			break;

		default:
			fprintf(stderr, "unknown option %c (use -h for help)\n", c);
		case 'h':
			usage();
		}
	}

	fsinit();
	srv = sp_socksrv_create_tcp(&port);
	if (!srv) 
		goto error;

	srv->dotu = 1;
	srv->msize = msize;
	srv->attach = spfile_attach;
	srv->clone = xclone;
	srv->walk = xwalk;
	srv->open = xopen;
	srv->create = xcreate;
	srv->read = xread;
	srv->write = xwrite;
	srv->clunk = xclunk;
	srv->remove = xremove;
	srv->stat = xstat;
	srv->wstat = xwstat;
	srv->flush = xflush;
	srv->fiddestroy = xfiddestroy;
	srv->debuglevel = debuglevel;
	srv->treeaux = root;
	sp_srv_start(srv);

	if (!debuglevel) {
		close(0);
		open("/dev/null", O_RDONLY);
		close(1);
		open("/dev/null", O_WRONLY);
		close(2);
		open("/dev/null", O_WRONLY);

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "cannot fork\n");
			return -1;
		}

		if (pid != 0) {
			/* parent */
			return 0;
		}

		/* child */
		setsid();
		chdir("/");
	}

	sact.sa_handler = SIG_IGN;
	sigemptyset(&sact.sa_mask);
	sact.sa_flags = 0;
	if (sigaction(SIGPIPE, &sact, NULL) < 0) {
		sp_uerror(errno);
		goto error;
	}

	sact.sa_handler = sigchld_handler;
	if (sigaction(SIGCHLD, &sact, NULL) < 0) {
		sp_uerror(errno);
		goto error;
	}
	sigchld_setup_pipe();

	sp_poll_loop();
	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return -1;
}
