//#define _XOPEN_SOURCE 600
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
#include "strutil.h"
#include "xcpu.h"
#include "xcpufs.h"


enum {
	/* root level files */
	Qroot = 1,
	Qclone,
	Quname,
	Qarch,
	Qenv,
	Qprocs,

	/* session level files */
	Qctl = 1,
	Qexec,
	Qargv,
	Qsenv,
	Qstdin,
	Qstdout,
	Qstderr,
	Qstdio,
	Qwait,
	Qfs,
};

/* states */
enum {
	Initializing,
	Running,
	Finished,
	Wiped = 16,
};

/* modes */
enum {
	Normal,
	Persistent,
};

static Npfile *dir_first(Npfile *);
static Npfile *dir_next(Npfile *, Npfile *);
static void session_destroy(Npfile *);
static int clone_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int clone_openfid(Npfilefid *);
static void clone_closefid(Npfilefid *);
static int clone_wstat(Npfile *, Npstat *);
static int procs_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int procs_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int procs_openfid(Npfilefid *);
static void procs_closefid(Npfilefid *);
static int session_wstat(Npfile *, Npstat *);
static int ctl_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int ctl_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int ctl_wstat(Npfile *, Npstat *);
static int exec_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int exec_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int exec_openfid(Npfilefid *);
static void exec_closefid(Npfilefid *);
static int exec_wstat(Npfile *, Npstat *);
static int filebuf_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int filebuf_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int filebuf_wstat(Npfile *, Npstat *);
static int filepipe_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int filepipe_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int stdio_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static int stdio_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int wait_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static void reftrack_ref(Npfile *, Npfilefid *);
static void reftrack_unref(Npfile *, Npfilefid *);
static int signame2signo(char *sig);

static int xclone(Npfid *fid, Npfid *newfid);
static int xwalk(Npfid *fid, Npstr *wname, Npqid *wqid);
static Npfcall *xopen(Npfid *fid, u8 mode);
static Npfcall *xcreate(Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension);
static Npfcall* xread(Npfid *fid, u64 offset, u32 count, Npreq *);
static Npfcall* xwrite(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *);
static Npfcall* xclunk(Npfid *fid);
static Npfcall* xremove(Npfid *fid);
static Npfcall* xstat(Npfid *fid);
static Npfcall* xwstat(Npfid *fid, Npstat *stat);
static void xfiddestroy(Npfid *fid);

Npauth xauth = {
	.auth = xauth_auth,
	.attach = xauth_attach,
	.read = xauth_read,
	.write = xauth_write,
	.clunk = xauth_clunk,
};

Npdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Npdirops session_ops = {
	.first = dir_first,
	.next = dir_next,
	.wstat = session_wstat,
	.destroy = session_destroy,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops clone_ops = {
	.read = clone_read,
	.wstat = clone_wstat,
	.openfid = clone_openfid,
	.closefid = clone_closefid,
};

Npfileops procs_ops = {
	.read = procs_read,
	.write = procs_write,
	.openfid = procs_openfid,
	.closefid = procs_closefid,
};

Npfileops ctl_ops = {
	.read = ctl_read,
	.write = ctl_write,
	.wstat = ctl_wstat,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops exec_ops = {
	.read = exec_read,
	.write = exec_write,
	.wstat = exec_wstat,
	.openfid = exec_openfid,
	.closefid = exec_closefid,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops argv_ops = {
	.read = filebuf_read,
	.write = filebuf_write,
	.wstat = filebuf_wstat,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops env_ops = {
	.read = filebuf_read,
	.write = filebuf_write,
	.wstat = filebuf_wstat,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops stdin_ops = {
	.read = filepipe_read,
	.write = filepipe_write,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops stdout_ops = {
	.read = filepipe_read,
	.write = filepipe_write,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops stderr_ops = {
	.read = filepipe_read,
	.write = filepipe_write,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops stdio_ops = {
	.read = stdio_read,
	.write = stdio_write,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops wait_ops = {
	.read = wait_read,
	.ref = reftrack_ref,
	.unref = reftrack_unref,
};

Npfileops fbuf_ops = {
	.read = filebuf_read,
	.write = filebuf_write,
	.wstat = filebuf_wstat,
};

Npfileops fpipe_ops = {
	.read = filepipe_read,
	.write = filepipe_write,
};

extern char **environ;
extern int sameuser;

static pthread_mutex_t session_lock = PTHREAD_MUTEX_INITIALIZER;
static int session_next_id;
static Xsession *sessions;
static Npfile *root;
static Npuser *user;
static Xfilebuf unamebuf;
static Xfilebuf archbuf;
static pthread_mutex_t env_lock = PTHREAD_MUTEX_INITIALIZER;
static Xfilebuf envbuf;
static char *tmppath = "/tmp";
static pthread_t wait_thread;
static pthread_cond_t exec_cond = PTHREAD_COND_INITIALIZER;

static int debuglevel;
Npsrv *srv;

static void
bufinit(Xfilebuf *fbuf)
{
	fbuf->size = 0;
	fbuf->buf = NULL;
}

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
bufwrite(Xfilebuf *fbuf, u64 offset, u32 count, u8 *data)
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

static int
bufsize(Xfilebuf *fbuf)
{
	return fbuf->size;
}

static void
bufsetsize(Xfilebuf *fbuf, int size)
{
	if (fbuf->size < size)
		bufwrite(fbuf, size, 0, NULL);
	else
		fbuf->size = size;
}

static void
buffree(Xfilebuf *fbuf)
{
	fbuf->size = 0;
	free(fbuf->buf);
}

static void
create_rerror(int ecode)
{
	char buf[256];

	strerror_r(ecode, buf, sizeof(buf));
	np_werror(buf, ecode);
}

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
filebuf_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xfilebuf *fbuf;

	fbuf = fid->file->aux;
	return bufread(fbuf, offset, count, data);
}

static int
filebuf_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xfilebuf *fbuf;

	fbuf = fid->file->aux;
	return bufwrite(fbuf, offset, count, data);
}

static int
filebuf_wstat(Npfile *f, Npstat *stat)
{
	Xfilebuf *fbuf;

	fbuf = f->aux;
	if (f->length != stat->length)
		bufsetsize(fbuf, stat->length);

	return 1;
}

static int
filepipe_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xfilepipe *p;

	p = fid->file->aux;
	if (p->direction != Read) {
		np_werror("Cannot read", EPERM);
		return 0;
	}

	return pip_addreq(p, req)?-1:0;
}

static int
filepipe_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xfilepipe *p;

	p = fid->file->aux;
	if (p->direction != Write) {
		np_werror("Cannot write", EPERM);
		return 0;
	}

	return pip_addreq(p, req)?-1:0;
}

Xsession*
session_create(int msize)
{
	int n;
	char *buf;
	Xsession *s, *ps, *xs;

	xs = malloc(sizeof(*xs));
	if (!xs)
		return NULL;

	pthread_mutex_init(&xs->lock, NULL);
	bufinit(&xs->argv);
	bufinit(&xs->env);
	xs->ctl = NULL;
	xs->refcount = 1;
	xs->state = Initializing;
	xs->mode = Normal;
	xs->stin = pip_create(Write);
	xs->stout = pip_create(Read);
	xs->sterr = pip_create(Read);
	xs->execpath = NULL;
	xs->pid = -1;
	xs->exitstatus = NULL;
	xs->waitreqs = NULL;
	n = strlen(tmppath) + 16;
	buf = malloc(n);
	if (!buf) {
		free(xs);
		return NULL;
	}

	sprintf(buf, "%s/xcpu-XXXXXX", tmppath);
	xs->dirpath = mkdtemp(buf);
	if (!xs->dirpath) {
		free(buf);
		free(xs);
		return NULL;
	}

	pthread_mutex_lock(&session_lock);
	xs->id = session_next_id;
	session_next_id++;
	if (session_next_id == INT_MAX) {
		for(session_next_id = 0, s = sessions; s != NULL && s->id == session_next_id; 
			s = s->next, session_next_id++);
	}

	/* keep sessions ordered by id */
	for(ps=NULL, s=sessions; s!=NULL && s->id<xs->id; ps = s, s = s->next)
		;

	if (ps) {
		xs->next = ps->next;
		ps->next = xs;
	} else {
		xs->next = sessions;
		sessions = xs;
	}
	pthread_mutex_unlock(&session_lock);

	return xs;
}

static void
session_destroy(Npfile *file)
{
	Xsession *xs, *s, *ps;

	xs = file->aux;
//	fprintf(stderr, "session_destroy %p\n", xs);
	pthread_mutex_lock(&session_lock);
	for(s=sessions, ps=NULL; s!=NULL && s!=xs; ps=s, s=s->next)
		;

	if (ps)
		ps->next = xs->next;
	else
		sessions = xs->next;
	pthread_mutex_unlock(&session_lock);

	buffree(&xs->argv);
	buffree(&xs->env);
	free(xs->ctl);
	pip_destroy(xs->stin);
	pip_destroy(xs->stout);
	pip_destroy(xs->sterr);

	if (xs->execpath) {
		unlink(xs->execpath);
		free(xs->execpath);
	}

	if (xs->dirpath) {
		xrmdir(xs->dirpath);
		free(xs->dirpath);
	}

	if (xs->pid != -1)
		kill(xs->pid, SIGTERM);

	free(xs->exitstatus);
	free(xs);
}

static void
session_wipe(Xsession *xs)
{
	int n;
	Npfile *f, *f1, *s;
	Npreq *req;
	Npfcall *rc;
	Xwaitreq *wreq, *wreq1;

//	fprintf(stderr, "session_wipe %p\n", xs);
	pthread_mutex_lock(&xs->lock);
	if (xs->state == Wiped) {
		pthread_mutex_unlock(&xs->lock);
		return;
	}

	xs->state = Wiped;
	pthread_mutex_unlock(&xs->lock);

	pthread_mutex_lock(&root->lock);
	s = xs->file;
	if (root->dirfirst == s)
		root->dirfirst = s->next;
	else
		s->prev->next = s->next;
	if (s->next)
		s->next->prev = s->prev;
	if (s == root->dirlast)
		root->dirlast = s->prev;

	s->prev = s->next = s->parent = NULL;
//	npfile_decref(s);
	pthread_mutex_unlock(&root->lock);

	// remove the children
	pthread_mutex_lock(&s->lock);
	f = s->dirfirst;
	s->dirfirst = s->dirlast = NULL;
	pthread_mutex_unlock(&s->lock);

	while (f != NULL) {
		f1 = f->next;
		npfile_decref(f->parent);
		npfile_decref(f);
		f = f1;
	}

	//fprintf(stderr, "refcount %d\n", s->refcount);
	pthread_mutex_lock(&xs->lock);
	if (xs->pid != -1)
		kill(xs->pid, SIGTERM);
	wreq = xs->waitreqs;
	xs->waitreqs = NULL;
	pthread_mutex_unlock(&xs->lock);

	/* respond to all pending reads on "wait" */
	while (wreq != NULL) {
		req = wreq->req;
		rc = np_alloc_rread(req->tcall->count);
		n = cutstr(rc->data, req->tcall->offset, 
			req->tcall->count, xs->exitstatus, 0);
		np_set_rread_count(rc, n);
		np_respond(req, rc);
		wreq1 = wreq->next;
		free(wreq);
		wreq = wreq1;
	}
}

int
session_incref(Xsession *xs)
{
	int ret;

	pthread_mutex_lock(&xs->lock);
	//fprintf(stderr, "session_incref refcount %d\n", xs->refcount + 1);
	ret = ++xs->refcount;
	pthread_mutex_unlock(&xs->lock);

	return ret;
}

void
session_decref(Xsession *xs)
{
	int ref, wipe;

	pthread_mutex_lock(&xs->lock);
	//fprintf(stderr, "session_decref refcount %d\n", xs->refcount - 1);
	xs->refcount--;
	ref = xs->refcount;
	wipe = !xs->refcount && xs->mode==Normal && xs->state != Running;
	pthread_mutex_unlock(&xs->lock);

	if (wipe) 
		session_wipe(xs);

	if (!ref) {
//		fprintf(stderr, "session_decref xs %p file refcount %d\n", xs, xs->file->refcount);
		npfile_decref(xs->file);
	}
}

static int
session_wstat(Npfile *f, Npstat *st)
{
	char *uname;
	Npuser *user;
	Npfile *c;

	uname = np_strdup(&st->uid);
	user = np_uname2user(uname);
	free(uname);

	if (!user) {
		np_werror("invalid user", EIO);
		return 0;
	}

	pthread_mutex_lock(&f->lock);
	for(c = f->dirfirst; c != NULL; c = c->next) {
		c->uid = c->muid = user;
		c->gid = user->dfltgroup;
	}
	pthread_mutex_unlock(&f->lock);

	return 1;
}


static Npfile *
create_file(Npfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Npuser *usr, void *aux)
{
	Npfile *ret;

	ret = npfile_alloc(parent, name, mode, qpath, ops, aux);
	if (parent) {
		pthread_mutex_lock(&parent->lock);
		if (parent->dirlast) {
			parent->dirlast->next = ret;
			ret->prev = parent->dirlast;
		} else
			parent->dirfirst = ret;

		parent->dirlast = ret;
		if (!usr)
			usr = parent->uid;
		pthread_mutex_unlock(&parent->lock);
	}

	if (!usr)
		usr = user;

	ret->atime = ret->mtime = time(NULL);
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
	npfile_incref(ret);
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

	/* why are the two identical ??? */
	bufwrite(&unamebuf, 0, strlen(buf), (u8 *) buf);
	bufwrite(&archbuf, 0, strlen(buf), (u8 *) buf);
	free(buf);
}

static void
envinit()
{
	int n;
	char *s, **e;

	for(n = 0, e = environ; *e != NULL; e++) {
		s = quotestrdup(*e);
		n += bufwrite(&envbuf, n, strlen(s), (u8 *) s);
		n += bufwrite(&envbuf, n, 1, (u8 *) "\n");
		free(s);
	}
}

static void
fsinit()
{
	archinit();
	envinit();

	root = npfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	npfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;

	create_file(root, "clone", 0444, Qclone, &clone_ops, NULL, NULL);
	create_file(root, "uname", 0444, Quname, &fbuf_ops, NULL, &unamebuf);
	create_file(root, "arch", 0444, Qarch, &fbuf_ops, NULL, &archbuf);
	create_file(root, "env", 0644, Qenv, &fbuf_ops, NULL, &envbuf);
}

static Npfile*
dir_first(Npfile *dir)
{
	npfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Npfile*
dir_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}


static int
clone_openfid(Npfilefid *fid)
{
	u64 qpath;
	char buf[16];
	Xsession *xs;
	Npfile *session;

	xs = session_create(fid->fid->conn->msize);
	snprintf(buf, sizeof buf, "%d", xs->id);
	qpath = QPATH(xs->id);
	session = create_file(root, buf, 0555 | Dmdir, qpath, &session_ops, 
		fid->fid->user, xs);
//	npfile_incref(session);
	xs->file = session;
	create_file(session, "ctl", 0666, qpath | Qctl, &ctl_ops, NULL, xs);
	create_file(session, "exec", 0666, qpath | Qexec, &exec_ops, NULL, NULL);
	create_file(session, "argv", 0666, qpath | Qargv, &argv_ops, NULL, &xs->argv);
	create_file(session, "env", 0666, qpath | Qsenv, &env_ops, NULL, &xs->env);
	create_file(session, "stdin", 0666, qpath | Qstdin, &stdin_ops, NULL, xs->stin);
	create_file(session, "stdout", 0444, qpath | Qstdout, &stdout_ops, NULL, xs->stout);
	create_file(session, "stderr", 0444, qpath | Qstderr, &stderr_ops, NULL, xs->sterr);
	create_file(session, "stdio", 0666, qpath | Qstdio, &stdio_ops, NULL, xs->stin);
	create_file(session, "wait", 0666, qpath | Qwait, &wait_ops, NULL, NULL);
	xs->fsfile = create_file(session, "fs", 0777, qpath | Qfs, NULL, NULL, xs);

//	npfile_incref(session);
	//fprintf(stderr, "clone_openfid sref %d\n", xs->file->refcount);
	fid->aux = session;
	return 1;
}

static void
clone_closefid(Npfilefid *fid) {
	Npfile *session;
	Xsession *xs;

	session = fid->aux;
	xs = session->aux;
//	npfile_decref(session);
	session_decref(xs);
}

static int
clone_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;
	char buf[16];

	xs = ((Npfile *) fid->aux)->aux;
	snprintf(buf, sizeof buf, "%d", xs->id);

	return cutstr(data, offset, count, buf, 0);
}

static int
clone_wstat(Npfile *f, Npstat *st)
{
	char *s;
	Npuser *user;
	Npgroup *group;

	if (st->n_uid != -1) {
		user = np_uid2user(st->n_uid);
		if (!user) {
			np_werror("unknown user id", EIO);
			return 0;
		}
	}

	if (st->n_gid != -1) {
		group = np_gid2group(st->n_gid);
		if (!user) {
			np_werror("unknown group id", EIO);
			return 0;
		}
	}

	if (!user && st->uid.len != 0) {
		s = np_strdup(&st->uid); 
		user = np_uname2user(s);
		free(s);
		if (!user) {
			np_werror("unknown user name", EIO);
			return 0;
		}
	}

	if (!group && st->gid.len != 0) {
		s = np_strdup(&st->gid);
		group = np_gname2group(s);
		free(s);
		if (!group) {
			np_werror("unknown group name", EIO);
			return 0;
		}
	}

	if (st->mode != (u32)~0)
		f->mode = st->mode;

	if (user)
		f->uid = user;

	if (group)
		f->gid = group;

	return 1;
}

static int
procs_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	np_werror("not implemented", EIO);
	return 0;
}

static int
procs_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	np_werror("not implemented", EIO);
	return 0;
}

static int
procs_openfid(Npfilefid *fid)
{
	return 1;
}

static void
procs_closefid(Npfilefid *fid)
{
}

static int
xexec(Xsession *xs, char *exec)
{
	int pid, err;
	char *argv, *env;
	char **argvtok, **envtok;
	int pip[2];
	char buf;

	if (!exec /*xs->execpath */) {
		np_werror("executable not found", EIO);
		return 0;
	}

	pthread_mutex_lock(&env_lock);
	pthread_mutex_lock(&xs->lock);
	if (xs->state == Wiped) {
		np_werror("session wiped", EIO);
		pthread_mutex_unlock(&xs->lock);
		pthread_mutex_unlock(&env_lock);
		return 0;
	}

	xs->state = Running;
	env = malloc(envbuf.size + xs->env.size + strlen(xs->dirpath) + 10);
	memcpy(env, envbuf.buf, envbuf.size);
	memcpy(env + envbuf.size, xs->env.buf, xs->env.size);
	sprintf(env + envbuf.size + xs->env.size, "XCPUPATH=%s", xs->dirpath);
	free(xs->env.buf);
	xs->env.buf = env;
	xs->env.size = envbuf.size + xs->env.size + strlen(xs->dirpath) + 9;
	pthread_mutex_unlock(&xs->lock);
	pthread_mutex_unlock(&env_lock);

	env = malloc(xs->env.size + 1);
	memcpy(env, xs->env.buf, xs->env.size);
	env[xs->env.size] = '\0';
	if (tokenize(env, &envtok) < 0) {
		np_werror("environment bad format", EIO);
		return 0;
	}

	argv = malloc(xs->argv.size + 1);
	memcpy(argv, xs->argv.buf, xs->argv.size);
	argv[xs->argv.size] = '\0';
	if (tokenize(argv, &argvtok) < 0) {
		np_werror("arguments bad format", EIO);
		return 0;
	}

	pipe(pip);
	pid = fork();
	if (pid == -1) {
		np_werror("cannot fork", EIO);
		return 0;
	} else if (pid == 0) {
		/* child */
		close(pip[1]);
		read(pip[0], &buf, 1);
		close(pip[0]);
		close(0);
		close(1);
		close(2);
		dup2(xs->stin->rfd, 0);
		dup2(xs->stout->rfd, 1);
		dup2(xs->sterr->rfd, 2);
//		pip_close_local(xs->stdin);
//		pip_close_local(xs->stdout);
//		pip_close_local(xs->stderr);

		chdir(xs->dirpath);
		err = execve(exec, argvtok, envtok);
		exit(err);
	}

	/* parent */
	free(envtok);
	free(argvtok);
	free(env);
	free(argv);
	pthread_mutex_lock(&xs->lock);
	pip_close_remote(xs->stin);
	pip_close_remote(xs->stout);
	pip_close_remote(xs->sterr);
	xs->pid = pid;
	pthread_mutex_unlock(&xs->lock);
	close(pip[0]);
	buf = 5;
	write(pip[1], &buf, 1);
	close(pip[1]);
	pthread_cond_broadcast(&exec_cond);
	return 1;
}

static int
execute_command(Xsession *xs, char *s)
{
	int i, n, len, ret, nargs;
	int fd;
	char *buf, *execpath;
	char **toks, *cmd, **args;

	nargs = tokenize(s, &toks);
	if (nargs < 0) {
		np_werror("invalid format", EIO);
		return 0;
	}

	cmd = toks[0];
	args = &toks[1];

	ret = 0;
	if (strcmp("exec", cmd) == 0) {
//		chmod(xs->execpath, 0500);
		execpath = args[0];
		if (!execpath)
			execpath = xs->execpath;

		ret = xexec(xs, execpath);
/*
	} else if (strcmp("lexec", cmd) == 0) {
		fd = open(xs->execpath, O_RDONLY);
		if (fd == -1) {
			np_werror("cannot open exec file", EIO);
			goto done;
		}

		len = lseek(fd, 0, SEEK_END);
		lseek(fd, 0, SEEK_SET);
		buf = malloc(len + 1);
		if (!buf) {
			np_werror(Enomem, ENOMEM);
			goto done;
		}

		n = 0;
		while ((i = read(fd, buf + n, len - n)) > 0)
			n += i;

		if (n != len) {
			np_werror("error while reading executable path", EIO);
			goto done;
		}

		buf[len] = '\0';
		ret = xexec(xs, buf);
		free(buf);
*/
	} else if (strcmp("clone", cmd) == 0) {
		if (!args[0] || !args[1]) {
			np_werror("too few arguments", EIO);
			ret = 0;
			goto done;
		}

		n = strtol(args[0], &buf, 10);
		if (*buf != '\0') {
			np_werror("invalid maxsession format", EIO);
			ret = 0;
			goto done;
		}

		if (tspawn(xs, n, args[1]) < 0)
			ret = 0;
		else
			ret = 1;
	} else if (strcmp("wipe", cmd) == 0) {
		session_wipe(xs);
		ret = 1;
	} else if (strcmp("signal", cmd) == 0) {
		n = signame2signo(args[0]);
		if (n < 0) {
			np_werror("unsupported signal", EIO);
			goto done;
		}

		if (xs->pid != -1)
			kill(xs->pid, n);
		ret = 1;
	} else if (strcmp("type", cmd) == 0) {
		n = Normal;
		if (strcmp("normal", args[0]) == 0)
			n = Normal;
		else if (strcmp("persistent", args[0]) == 0)
			n = Persistent;
		else {
			np_werror("invalid session type", EIO);
			goto done;
		}

		pthread_mutex_lock(&xs->lock);
		xs->mode = n;
		pthread_mutex_unlock(&xs->lock);
		ret = 1;
	} else if (strcmp("close", cmd) == 0) {
		ret = 1;
	} else {
		np_werror("unknown command", EIO);
		ret = 0;
	}

done:
	free(toks);
	return ret;
}

static int
ctl_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;
	char buf[16];

	xs = fid->file->parent->aux;
	snprintf(buf, sizeof buf, "%d", xs->id);

	return cutstr(data, offset, count, buf, 0);
}

static int
ctl_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n, len;
	char *s, *t, *p, *ctl;
	Xsession *xs;

	xs = fid->file->parent->aux;
	pthread_mutex_lock(&xs->lock);
	len = xs->ctl?strlen(xs->ctl):0;
	s = realloc(xs->ctl, len + count + 1);
	if (!s) {
		np_werror(Enomem, ENOMEM);
		pthread_mutex_unlock(&xs->lock);
		return 0;
	}

	xs->ctl = s;
	memcpy(xs->ctl + len, data, count);
	xs->ctl[len + count] = '\0';
	p = strrchr(xs->ctl, '\n');
	if (!p) {
		pthread_mutex_unlock(&xs->lock);
		return count;
	}

	ctl = xs->ctl;
	xs->ctl = NULL;
	p++;
	if (*p != '\0') {
		t = malloc(strlen(p) + 1);
		if (!t) {
			np_werror(Enomem, ENOMEM);
			pthread_mutex_unlock(&xs->lock);
			return 0;
		}
		
		strcpy(t, p);
		xs->ctl = t;
	}
	pthread_mutex_unlock(&xs->lock);

	s = ctl;
	while ((t = strchr(s, '\n')) != NULL) {
		*t = '\0';
		if (!execute_command(xs, s)) {
			return 0;
		}

		s = t + 1;
	}
	free(ctl);

	return count;
}

static int 
ctl_wstat(Npfile *f, Npstat *st) {
	Xsession *xs;

	xs = f->parent->aux;
	if (st->length == 0) {
		pthread_mutex_lock(&xs->lock);
		free(xs->ctl);
		xs->ctl = NULL;
		pthread_mutex_unlock(&xs->lock);

		return 1;
	}

	np_werror("unsupported operation", EPERM);
	return 0;
}


static int
exec_openfid(Npfilefid *fid)
{
	int fd, n;
	Xsession *xs;

	fd = -1;
	xs = fid->file->parent->aux;
	if (fid->omode & Owrite) {
		pthread_mutex_lock(&xs->lock);
		if (xs->execpath) {
			np_werror("Cannot reopen exec for writing", EIO);
 			pthread_mutex_unlock(&xs->lock);
			return 0;
		}

		n = strlen(xs->dirpath) + 16;
		xs->execpath = malloc(n);
		snprintf(xs->execpath, n, "%s/exec", xs->dirpath);
		fd = open(xs->execpath, O_WRONLY | O_CREAT, 0700);
		if (fd < 0) {
			np_werror("Cannot create file", EPERM);
 			pthread_mutex_unlock(&xs->lock);
			return 0;
		}
		fchown(fd, fid->fid->user->uid, fid->fid->user->dfltgroup->gid);
		pthread_mutex_unlock(&xs->lock);
	} else {
		if (xs->execpath)
			fd = open(xs->execpath, O_RDONLY);
		else {
			np_werror("Nothing written to exec", EIO);
			return 0;
		}
	}

	xs->execfd = fd;

	fid->aux = xs;
	session_incref(xs);

	//fprintf(stderr, "exec_openfid path %s fd %d\n", xs->execpath, fd);
	return 1;
}

static void
exec_closefid(Npfilefid *fid)
{
	Xsession *xs;

	xs = fid->aux;
	//fprintf(stderr, "exec_closefid fd %d\n", xs->execfd);
	if (xs->execfd != -1)
		close(xs->execfd);

	xs->execfd = -1;
	session_decref(xs);
}

static int
exec_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;
	int n;

	xs = fid->aux;
	//fprintf(stderr, "exec_read fd %d\n", xs->execfd);
	n = pread(xs->execfd, data, count, offset);
	if (n == -1) {
		create_rerror(errno);
		return 0;
	}

	return n;
}

static int
exec_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;
	int n;

	xs = fid->aux;
	//fprintf(stderr, "exec_write fd %d\n", xs->execfd);
	n = pwrite(xs->execfd, data, count, offset);
	if (n == -1) {
		create_rerror(errno);
		return 0;
	}

	return n;
}

static int 
exec_wstat(Npfile *f, Npstat *st) {
	Xsession *xs;

	xs = f->parent->aux;
	if (xs->execpath == NULL)
		return 1;

	return 0;
}


static int
stdio_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;

	xs = fid->file->parent->aux;
	return pip_addreq(xs->stout, req);
}

static int
stdio_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Xsession *xs;

	xs = fid->file->parent->aux;
	return pip_addreq(xs->stin, req);
}

static int 
wait_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	Xwaitreq *wreq;
	Xsession *xs;

	xs = fid->file->parent->aux;
	pthread_mutex_lock(&xs->lock);
	if (xs->state==Finished || xs->state==Wiped) {
		n = cutstr(data, offset, count, xs->exitstatus, 0);
		pthread_mutex_unlock(&xs->lock);
		return n;
	}

	wreq = malloc(sizeof(*wreq));
	if (!wreq) {
		np_werror(Enomem, ENOMEM);
		pthread_mutex_unlock(&xs->lock);
		return 0;
	}

	wreq->req = req;
	wreq->next = xs->waitreqs;
	xs->waitreqs = wreq;
	pthread_mutex_unlock(&xs->lock);
	return -1;
}

static void
wait_flush(Xsession *xs, Npreq *req)
{
	Xwaitreq *wreq, *pwreq;

	pthread_mutex_lock(&xs->lock);
	for(pwreq = NULL, wreq = xs->waitreqs; wreq != NULL; pwreq=wreq, wreq = wreq->next)
		if (wreq->req == req)
			break;

	if (wreq) {
		if (pwreq)
			pwreq->next = wreq->next;
		else
			xs->waitreqs = wreq->next;

		np_respond(req, NULL);
		free(wreq);
	}
	pthread_mutex_unlock(&xs->lock);
}


/*
static int
reftrack_openfid(Npfilefid *fid)
{
	Xsession *xs;

	xs = fid->file->parent->aux;
//	npfile_incref(fid->file->parent);
	npfile_incref(xs->file);
	//fprintf(stderr, "reftrack_openfid %p\n", xs);
	session_incref(xs);
	return 1;
}

static void
reftrack_closefid(Npfilefid *fid)
{
	Xsession *xs;

	xs = fid->file->parent->aux;
	//fprintf(stderr, "reftrack_closefid %p\n", xs);
	session_decref(xs);
//	npfile_decref(fid->file->parent);
	npfile_decref(xs->file);
}

static void
reftrack_destroy(Npfile *f)
{
	Xsession *xs;

	xs = f->parent->aux;
	session_decref(xs);
}
*/

static void
reftrack_ref(Npfile *file, Npfilefid *fid)
{
	Xsession *xs;

	if ((file->qid.path&QMASK) == 0)
		xs = file->aux;
	else
		xs = file->parent->aux;

	//fprintf(stderr, "reftrack_ref %s %d sref %d\n", file->name, file->refcount, xs->file->refcount);
	session_incref(xs);
}

static void 
reftrack_unref(Npfile *file, Npfilefid *fid)
{
	Xsession *xs;

	if ((file->qid.path&QMASK) == 0)
		xs = file->aux;
	else
		xs = file->parent->aux;

	//fprintf(stderr, "reftrack_unref %s %d sref %d\n", file->name, file->refcount-1, xs->file->refcount);
	session_decref(xs);
}

void
usage()
{
	//fprintf(stderr, "xcpufs: -d -p port -w nthreads\n");
	exit(-1);
}

static void
xflush(Npreq *req)
{
	Xsession *xs;
	Npfilefid *fid;
	Npfile *f;

	if (!req->fid || !req->fid->aux)
		return;

	fid = req->fid->aux;
	f = fid->file;
	xs = f->parent->aux;

	switch (f->qid.path & 255) {
	case Qstdin:
		pip_flushreq(xs->stin, req);
		break;

	case Qstdout:
		pip_flushreq(xs->stout, req);
		break;

	case Qstderr:
		pip_flushreq(xs->sterr, req);
		break;

	case Qstdio:
		if (req->tcall->type == Tread) 
			pip_flushreq(xs->stout, req);
		else
			pip_flushreq(xs->stin, req);
		break;

	case Qwait:
		wait_flush(xs, req);
		break;

	}
}

static void *
wait_proc(void *a)
{
	Xsession *s;
	Xwaitreq *wreq, *wreq1;
	Npreq *req;
	Npfcall *rc;
	int pid, status, n, wipe;

//	fprintf(stderr, "wait_proc pthread %d\n", pthread_self());
	while (1) {
		pid = wait(&status);
		pthread_mutex_lock(&session_lock);
		if (pid==-1 && errno==ECHILD) {
			pthread_cond_wait(&exec_cond, &session_lock);
			pthread_mutex_unlock(&session_lock);
			continue;
		}

		for(s = sessions; s != NULL; s = s->next)
			if (s->pid == pid)
				break;
		pthread_mutex_unlock(&session_lock);

//		fprintf(stderr, "wait_proc: pid %d session %p\n", pid, s);
		if (!s)
			continue;

		session_incref(s);
		pthread_mutex_lock(&s->lock);
		if (s->state != Wiped)
			s->state = Finished;
		s->pid = -1;
		s->exitstatus = malloc(16);
		if (s->exitstatus) 
			sprintf(s->exitstatus, "%d", status);
		pip_close_local(s->stin);
		wreq = s->waitreqs;
		s->waitreqs = NULL;
		pthread_mutex_unlock(&s->lock);

		/* respond to all pending reads on "wait" */
		while (wreq != NULL) {
			req = wreq->req;
			rc = np_alloc_rread(req->tcall->count);
			n = cutstr(rc->data, req->tcall->offset, 
				req->tcall->count, s->exitstatus, 0);
			np_set_rread_count(rc, n);
			np_respond(req, rc);
			wreq1 = wreq->next;
			free(wreq);
			wreq = wreq1;
		}

		session_decref(s);
	}
}

static int 
xclone(Npfid *fid, Npfid *newfid)
{
	int ret;
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		ret = npfile_clone(fid, newfid);
	else
		ret = ufs_clone(fid, newfid);

	return ret;
}

static int 
xwalk(Npfid *fid, Npstr *wname, Npqid *wqid)
{
	int ret;
	Npfilefid *f;
	Xsession *xs;
	Fsfid *fsfid;

	f = fid->aux;
	if (f->file)
		ret = npfile_walk(fid, wname, wqid);
	else {
		fsfid = f->aux;
		xs = fsfid->xs;

		if (wname->len==2 && !memcmp(wname->str, "..", 2) 
		&& !strcmp(xs->dirpath, fsfid->path)) {
			f->file = xs->fsfile;
			npfile_incref(f->file);
			if (fsfid->fd != -1)
				close(fsfid->fd);
			if (fsfid->dir)
				closedir(fsfid->dir);

			free(fsfid->path);
			free(fsfid);
			f->aux = NULL;
			ret = npfile_walk(fid, wname, wqid);
		} else
			ret = ufs_walk(fid, wname, wqid);
	}

	f = fid->aux;
	if (f->file) {
		xs = f->file->aux;
		if (!f->file->ops) {
			npfile_decref(f->file);
			ufs_attach(fid, xs, wqid);
		}
	} 

	return ret;
}

static Npfcall *
xopen(Npfid *fid, u8 mode)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_open(fid, mode);
	else
		return ufs_open(fid, mode);
}

static Npfcall *
xcreate(Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_create(fid, name, perm, mode, extension);
	else
		return ufs_create(fid, name, perm, mode, extension);
}

static Npfcall *
xread(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_read(fid, offset, count, req);
	else
		return ufs_read(fid, offset, count, req);
}

static Npfcall *
xwrite(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_write(fid, offset, count, data, req);
	else
		return ufs_write(fid, offset, count, data, req);
}

static Npfcall*
xclunk(Npfid *fid)
{
	Npfilefid *f;
	Npfcall *ret;

	f = fid->aux;
	if (f->file)
		ret = npfile_clunk(fid);
	else
		ret = ufs_clunk(fid);

	return ret;
}

static Npfcall*
xremove(Npfid *fid)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_remove(fid);
	else
		return ufs_remove(fid);
}

static Npfcall*
xstat(Npfid *fid)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_stat(fid);
	else
		return ufs_stat(fid);
}

static Npfcall*
xwstat(Npfid *fid, Npstat *stat)
{
	Npfilefid *f;

	f = fid->aux;
	if (f->file)
		return npfile_wstat(fid, stat);
	else
		return ufs_wstat(fid, stat);
}

static void 
xfiddestroy(Npfid *fid)
{
	Npfilefid *f;

	f = fid->aux;
	if (!f)
		return;

	if (f->file)
		npfile_fiddestroy(fid);
	else
		ufs_fiddestroy(fid);
}


int 
main(int argc, char *argv[])
{
	int err, c;
	int port, nwthreads;
	char *s;

	signal(SIGPIPE, SIG_IGN);
	err = pthread_create(&wait_thread, NULL, wait_proc, NULL);
	if (err) {
		//fprintf(stderr, "cannot create thread: %d\n", err);
		return 1;
	}

	user = np_uid2user(getuid());
	port = XCPU_PORT;
	nwthreads = 16;
	while ((c = getopt(argc, argv, "dsp:w:s")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 't':
			tmppath = strdup(optarg);
			break;

		case 's':
			sameuser++;
			break;

		default:
			usage();
		}
	}

	fsinit();
	srv = np_socksrv_create_tcp(nwthreads, &port);
	if (!srv)
		return -1;

	srv->dotu = 1;
	srv->attach = npfile_attach;
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
	np_srv_start(srv);

	while (1) {
		sleep(100);
	}

	return 0;
}

struct {
	char*	name;
	int	num;
} signals[] = {
	{ "SIGHUP",      1} ,   /* Hangup (POSIX).  */
	{ "SIGINT",      2} ,   /* Interrupt (ANSI).  */
	{ "SIGQUIT",     3},   /* Quit (POSIX).  */
	{"SIGILL",      4},   /* Illegal instruction (ANSI).  */
	{"SIGTRAP",     5},   /* Trace trap (POSIX).  */
	{"SIGABRT",     6},   /* Abort (ANSI).  */
	{"SIGIOT",      6},   /* IOT trap (4.2 BSD).  */
	{"SIGBUS",      7},   /* BUS error (4.2 BSD).  */
	{"SIGFPE",      8},   /* Floating-point exception (ANSI).  */
	{"SIGKILL",     9},   /* Kill, unblockable (POSIX).  */
	{"SIGUSR1",     10},  /* User-defined signal 1 (POSIX).  */
	{"SIGSEGV",     11},  /* Segmentation violation (ANSI).  */
	{"SIGUSR2",     12},  /* User-defined signal 2 (POSIX).  */
	{"SIGPIPE",     13},  /* Broken pipe (POSIX).  */
	{"SIGALRM",     14},  /* Alarm clock (POSIX).  */
	{"SIGTERM",     15},  /* Termination (ANSI).  */
	{"SIGSTKFLT",   16},  /* Stack fault.  */
	{"SIGCLD",      17}, /* Same as SIGCHLD (System V).  */
	{"SIGCHLD",     17},  /* Child status has changed (POSIX).  */
	{"SIGCONT",     18},  /* Continue (POSIX).  */
	{"SIGSTOP",     19},  /* Stop, unblockable (POSIX).  */
	{"SIGTSTP",     20},  /* Keyboard stop (POSIX).  */
	{"SIGTTIN",     21},  /* Background read from tty (POSIX).  */
	{"SIGTTOU",     22},  /* Background write to tty (POSIX).  */
	{"SIGURG",      23},  /* Urgent condition on socket (4.2 BSD).  */
	{"SIGXCPU",     24},  /* CPU limit exceeded (4.2 BSD).  */
	{"SIGXFSZ",     25},  /* File size limit exceeded (4.2 BSD).  */
	{"SIGVTALRM",   26},  /* Virtual alarm clock (4.2 BSD).  */
	{"SIGPROF",     27},  /* Profiling alarm clock (4.2 BSD).  */
	{"SIGWINCH",    28},  /* Window size change (4.3 BSD, Sun).  */
	{"SIGPOLL",     29},   /* Pollable event occurred (System V).  */
	{"SIGIO",       29},  /* I/O now possible (4.2 BSD).  */
	{"SIGPWR",      30},  /* Power failure restart (System V).  */
	{"SIGSYS",      31},  /* Bad system call.  */
	{"SIGUNUSED",   31},
	{NULL, 0},
};


static int
signame2signo(char *sig)
{
	int i, n;
	char *e;

	n = strtol(sig, &e, 10);
	if (*e == '\0')
		return n;

	for(i = 0; signals[i].name != NULL; i++)
		if (strcmp(signals[i].name, sig) == 0)
			return signals[i].num;

	return -1;
}
