/*
 * Copyright (C) 2006, 2008 by Latchesar Ionkov <lucho@ionkov.net>
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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpu.h"

typedef struct Pconn Pconn;
typedef struct Pmsg Pmsg;
typedef struct Pkeyval Pkeyval;

struct Pconn {
	int		rank;
	int		fd;
	Spfd*		spfd;
	Pmsg*		rmsg;
	int		wpos;
	Pmsg*		wmsg;
};

struct Pmsg {
	int		size;
	int		nattrs;
	char**		attrs;
	int*		attrlen;
	char		data[1024];

	Pmsg*		next;
};

struct Pkeyval {
	char*		key;
	char*		val;

	Pkeyval*	next;
};

extern int spc_chatty;

int signo;
static Spfd *xmp_inspfd;
int xmp_inbuflen;
char xmp_inbuf[8192];
int xmp_inpipe[2];
pid_t xmp_inpid;
int ncmds;
Xpcommand **xcmds;
int shownode;

/* PMI variables */
int pmidebug;		/* show PMI messages */
int pminprocs;		/* number of procs */
Pconn *pmiconns;	/* PMI connections */
int pmisock;		/* PMI listen socket */
Spfd *pmifd;		/* PMI listen spfd */
char pmiaddr[512];	/* string representation of the listen socket */
int pmimaxrank;		/* current rank */
int pminbarriers;	/* number of clients that responded to a barrier */
Pkeyval *pmikvs;	/* key values */

static int setup_stdin_proc(int infd);
void xmp_stdout(Xpsession *s, u8 *buf, u32 buflen);
void xmp_stderr(Xpsession *s, u8 *buf, u32 buflen);
void xmp_wait(Xpsession *s, u8 *buf, u32 buflen);

/* PMI functions */
int pmisetup(char *laddr, int nprocs, int debug);
char *getpmiaddr(void);
static void pmirespond(Pconn *p, Pmsg *m);
Pkeyval *kvsfind(char *key);
int kvsput(char *key, char *val);
char *kvsget(char *key);

Pmsg *
pmsgalloc()
{
	Pmsg *ret;

	ret = sp_malloc(sizeof(Pmsg));
	if (!ret)
		return NULL;

	ret->size = 0;
	ret->nattrs = 0;
	ret->attrs = NULL;
	ret->attrlen = NULL;
	ret->next = NULL;

	return ret;
}

void
pmsgfree(Pmsg *pm)
{
	free(pm->attrs);
	free(pm->attrlen);
	free(pm);
}

int
pmsgconv(Pmsg *pm)
{
	int i;
	char *s;

	pm->nattrs = tokenize(pm->data, &pm->attrs);
	if (pm->nattrs < 0)
		return -1;

	pm->attrlen = sp_malloc(sizeof(int) * pm->nattrs);
	if (!pm->attrlen)
		return -1;

	for(i = 0; i < pm->nattrs; i++) {
		s = strchr(pm->attrs[i], '=');
		if (s)
			pm->attrlen[i] = s - pm->attrs[i];
		else
			pm->attrlen[i] = 0;
	}

	return 0;
}

char *
pmsgstr(Pmsg *pm, char *aname)
{
	int i, n;

	n = strlen(aname);
	for(i = 0; i < pm->nattrs; i++)
		if (pm->attrlen[i]==n && !memcmp(pm->attrs[i], aname, n))
			return pm->attrs[i] + pm->attrlen[i] + 1;

	return NULL;
}

int
pmsgint(Pmsg *pm, char *aname)
{
	int n;
	char *s, *p;

	s = pmsgstr(pm, aname);
	if (!s)
		return -1;

	n = strtol(s, &p, 10);
	if (*p != '\0')
		return -1;

	return n;
}

Pmsg *
pmsgcreate(char *cmd, int rc, char *more, ...)
{
	int n;
	Pmsg *m;
	va_list ap;

	va_start(ap, more);
	m = pmsgalloc();

	n = snprintf(m->data, sizeof(m->data), "cmd=%s rc=%d ", cmd, rc);
	if (more)
		n += vsnprintf(m->data+n, sizeof(m->data)-n, more, ap);

	n += snprintf(m->data+n, sizeof(m->data)-n, "\n");
	m->size = n;
	va_end(ap);

	return m;
}

static void
pmirequest(Pconn *p, Pmsg *m)
{
	int i;
	char *cmd, *k, *v;
	Pmsg *mr;

	if (pmidebug)
		fprintf(stderr, "-pmi-> %d: %s\n", p->rank, m->data);

	cmd = pmsgstr(m, "cmd");
	if (!cmd) {
		fprintf(stderr, "invalid request\n");
		return;
	}

	if (!strcmp(cmd, "initack")) {
		mr = pmsgcreate("initack", 0, NULL);
		pmirespond(p, mr);
		mr = pmsgcreate("set", 0, "size=%d", pminprocs);
		pmirespond(p, mr);
		mr = pmsgcreate("set", 0, "rank=%d", p->rank);
		pmirespond(p, mr);
		mr = pmsgcreate("set", 0, "debug=%d", 0);
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "init")) {
		mr = pmsgcreate("response_to_init", 0, NULL);
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "get_maxes")) {
		mr = pmsgcreate("maxes", 0,
			"kvsname_max=%d keylen_max=%d vallen_max=%d", 64, 64, 64);
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "get_appnum")) {
		mr = pmsgcreate("appnum", 0, "appnum=0");
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "get_my_kvsname")) {
		mr = pmsgcreate("my_kvsname", 0, "kvsname=kvs_0");
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "put")) {
		k = pmsgstr(m, "key");
		v = pmsgstr(m, "value");
		if (!k || !v) {
			mr = pmsgcreate("put_result", 1, NULL);
			pmirespond(p, mr);
		} else {
			kvsput(k, v);
			mr = pmsgcreate("put_result", 0, NULL);
			pmirespond(p, mr);
		}
	} else if (!strcmp(cmd, "get")) {
		k = pmsgstr(m, "key");
		v = kvsget(k);
		if (!k || !v) {
			mr = pmsgcreate("put_result", 1, NULL);
			pmirespond(p, mr);
		} else {
			mr = pmsgcreate("get_result", 0, "value=%s", v);
			pmirespond(p, mr);
		}
	} else if (!strcmp(cmd, "barrier_in")) {
		pminbarriers++;
		if (pminbarriers == pminprocs) {
			pminbarriers = 0;
			for(i = 0; i < pminprocs; i++) {
				mr = pmsgcreate("barrier_out", 0, NULL);
				pmirespond(&pmiconns[i], mr);
			}
		}
	} else if (!strcmp(cmd, "finalize")) {
		mr = pmsgcreate("finalize_ack", 0, NULL);
		pmirespond(p, mr);
	} else if (!strcmp(cmd, "get_universe_size")) {
		mr = pmsgcreate("universe_size", 0, "size=%d", pminprocs);
		pmirespond(p, mr);
	} else {
		mr = pmsgcreate("error", 1, "msg=unsupported-message");
		pmirespond(p, mr);
	}

	pmsgfree(m);
}

static void
pconnotify(Spfd *spfd, void *aux)
{
	int n;
	char *t;
	Pconn *p;
	Pmsg *m, *m1;

	p = aux;
	if (spfd_can_read(spfd)) {
		m = p->rmsg;
		if (!m)
			m = pmsgalloc();

		n = spfd_read(spfd, m->data + m->size, sizeof(m->data) - m->size);
		if (n < 0) {
			fprintf(stderr, "error while reading: %d\n", errno);
			goto error;
		}

		m->size += n;
		while (m->size>0 && (t = memchr(m->data, '\n', m->size)) != NULL) {
			*t = '\0';
			t++;
			m1 = pmsgalloc();
			m1->size = m->size - (t - m->data);
			memmove(m1->data, t + 1, m1->size);
			p->rmsg = m1;
			pmsgconv(m);
			pmirequest(p, m);
			m = p->rmsg;
		}
	}

	if (p->wmsg && spfd_can_write(spfd)) {
		m = p->wmsg;
		n = spfd_write(spfd, m->data + p->wpos, m->size - p->wpos);
		if (n < 0) {
			fprintf(stderr, "error while writing: %d\n", errno);
			goto error;
		}

		p->wpos += n;
		if (p->wpos == m->size) {
			p->wmsg = m->next;
			pmsgfree(m);
			p->wpos = 0;
		}
	}

	if (spfd_has_error(spfd)) {
error:
		spfd_remove(spfd);
		p->spfd = NULL;
		close(p->fd);
		p->fd = -1;
	}
}

static void
pmirespond(Pconn *p, Pmsg *m)
{
	Pmsg *m1;

	if (pmidebug)
		fprintf(stderr, "<-pmi- %d: %s", p->rank, m->data);

	if (!p->wmsg) {
		p->wmsg = m;
		pconnotify(p->spfd, p);
	} else {
		for(m1 = p->wmsg; m1->next != NULL; m1 = m1->next)
			;

		m1->next = m;
	}
}

static void
pminotify(Spfd *spfd, void *aux)
{
	int csock;
	struct sockaddr_in caddr;
	socklen_t caddrlen;
	char buf[64];
	Pconn *p;

	if (!spfd_can_read(spfd))
		return;

	spfd_read(spfd, buf, 0);
	caddrlen = sizeof(caddr);
	csock = accept(pmisock, (struct sockaddr *) &caddr, &caddrlen);
	if (csock < 0) {
		close(pmisock);
		fprintf(stderr, "error while accepting: %d\n", errno);
		return;
	}

	p = &pmiconns[pmimaxrank++];
	p->rank = pmimaxrank-1;
	p->fd = csock;
	p->spfd = spfd_add(csock, pconnotify, p);
	p->wpos = 0;
	p->rmsg = NULL;
	p->wmsg = NULL;
}

int
pmisetup(char *laddr, int npcs, int debug)
{
	socklen_t n;
	struct sockaddr_in saddr;

	pmidebug = debug;
	pminprocs = npcs;
	pmiconns = sp_malloc(pminprocs * sizeof(Pconn));
	if (!pmiconns)
		return -1;

	pmisock = socket(PF_INET, SOCK_STREAM, 0);
	if (pmisock < 0) {
		sp_suerror("cannot create socket", errno);
		return -1;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(0);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(pmisock, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		sp_suerror("cannot bind socket", errno);
		return -1;
	}

	if (listen(pmisock, 128) < 0) {
		sp_suerror("cannot listen on socket", errno);
		return -1;
	}

	n = sizeof(saddr);
	if (getsockname(pmisock, (struct sockaddr *) &saddr, &n) < 0) {
		sp_suerror("getsockname failed", errno);
		return -1;
	}

	spfd_add(pmisock, pminotify, NULL);
	if (laddr)
		snprintf(pmiaddr, sizeof(pmiaddr), "%s:%d", laddr, ntohs(saddr.sin_port));
	else {
		gethostname(pmiaddr, sizeof(pmiaddr));
		n = strlen(pmiaddr);
		snprintf(pmiaddr + n, sizeof(pmiaddr) - n, ":%d", ntohs(saddr.sin_port));
	}

	return 0;
}

char *
getpmiaddr(void)
{
	return pmiaddr;
}

Pkeyval*
kvsfind(char *key)
{
	Pkeyval *v;

	for(v = pmikvs; v != NULL; v = v->next)
		if (!strcmp(key, v->key))
			return v;

	return NULL;
}

int
kvsput(char *key, char *val)
{
	Pkeyval *v;

	v = kvsfind(key);
	if (!v) {
		v = sp_malloc(sizeof(*v));
		v->key = strdup(key);
		v->val = NULL;
		v->next = pmikvs;
		pmikvs = v;
	}

	free(v->val);
	v->val = strdup(val);

	return 0;
}

char *
kvsget(char *key)
{
	Pkeyval *v;

	v = kvsfind(key);
	if (!v)
		return NULL;

	return v->val;
}

void usage(char *argv0) {
	fprintf(stderr, "usage: %s [-d] [-n tsessions] [-k privkey] [-e exec] [-l] host!port,... copypath [arg1 ...]\n", argv0);
	exit(1);
}

static void
pass_signal(int sig)
{
	int i;

	for(i = 0; i < ncmds; i++)
		xp_command_kill(xcmds[i], sig);
}

char *
fullpath(char *name, char *sysroot){
	char *path;
	char *fullpath;
	char *start, *end;
	struct stat buf;

	if (strchr(name, '/'))
		return strdup(name);

	fullpath = sp_malloc(MAXPATHLEN);
	if (! fullpath)
		return NULL;

	if (!sysroot)
		sysroot = "";

	snprintf(fullpath, MAXPATHLEN, "%s%s", sysroot, name);

//	if (strchr(name, '/'))
//		return fullpath;

	/* special case. If it is a directory, don't mess with it */
	if (stat(fullpath, &buf)==0 && S_ISDIR(buf.st_mode))
		return fullpath;

	path = strdup(getenv("PATH"));
	if (! path) {
		sp_werror(Enomem, ENOMEM);
		return NULL;
	}

	start = path;
	for(end = start; start && *start; start = end){
		int okexec = 0;
		end = strchr(start, ':');
		if (end)
			*end++ = 0;
		snprintf(fullpath, MAXPATHLEN, "%s%s/%s", sysroot, start, name);
		if (stat(fullpath, &buf) < 0)
			continue;
		if (! S_ISREG(buf.st_mode))
			continue;
		/* but could we exec it */
		if (buf.st_mode && S_IXOTH)
			okexec = 1;

		if ((! okexec) && (buf.st_mode && S_IXGRP) && (buf.st_gid == getgid()))
			okexec = 1;

		if ((! okexec) && (buf.st_mode && S_IXUSR) && (buf.st_uid == getuid()))
			okexec = 1;
		if (! okexec)
			continue;

		/* rule: if the exec can't happen with this file, have to keep searching. */
		return fullpath;
	}

	return NULL;

}

/* stolen from xcpufs.c */
static char *
getarch(void)
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

	return buf;
}

static char *
get_argv(char *exec, char **argv, int argc)
{
	int i, n;
	char **pargs, *xargv;

	pargs = sp_malloc((argc + 1) * sizeof(char*));
	if (!pargs) 
		return NULL;

	memset(pargs, 0, (argc+1) * sizeof(char *));
	pargs[0] = quotestrdup(exec);
	if (!pargs[0]) {
		sp_werror(Enomem, ENOMEM);
		goto error;
	}

	n = strlen(pargs[0]) + 1;
	for(i = 0; i < argc; i++) {
		pargs[i + 1] = quotestrdup(argv[i]);
		if (!pargs[i + 1]) {
			sp_werror(Enomem, ENOMEM);
			goto error;
		}

		n += strlen(pargs[i+1]) + 1;
	}

	xargv = sp_malloc(n + 1);
	if (!xargv)
		goto error;

	*xargv = '\0';
	for(i = 0; i < argc + 1; i++) {
		strcat(xargv, pargs[i]);
		strcat(xargv, " ");
		free(pargs[i]);
	}

	free(pargs);
	xargv[strlen(xargv) - 1] = '\0';

	return xargv;

error:
	for(i = 0; i < argc; i++)
		free(pargs[i]);

	return NULL;
}

/* don't ask */
char *
process(char *s)
{
	char *b, *base;
	base = b = malloc(strlen(s)+1);
	/* now eat it up ... */
	while (*s){
		if (*s == '\\'){
			s++;
			switch(*s){
			case 'n':
				*b++ = '\n';
				break;
			}
			s++;
		} else
			*b++ = *s++;
	}
	return base;
}

int
main(int argc, char **argv)
{
	int i, n, c, ecode, maxsessions = 0, nocopy, copyshared, allflag = 0;
	int lstart, attach, linebuf, elen, pmidebug;
	char *ename, *s, *ndstr, *copypath, *xargv;
	char *env, *exec, *jobid, *sysroot, *thisarch, *arch;
	char *e, *addr;
	char *cwd, buf[1024];
	char archroot[MAXPATHLEN];
	char *homepath = NULL, *keypath = NULL;
	char *argv0 = argv[0];
	struct stat st;
	Xpnodeset *nds;
	Xpcommand *cmd;
	Spuser *user;
	Xkey *ukey;

	attach = 0;
	nocopy = 0;
	copyshared = 1;
	linebuf = 0;
	exec = NULL;
	jobid = NULL;
	cwd = NULL;
	pmidebug = 0;
	while ((c = getopt(argc, argv, "+dDn:e:j:c:J:lsSaLpm:k:")) != -1) {
		switch (c) {
		case 'd':
			spc_chatty = 1;
			break;

		case 'D':
			pmidebug = 1;
			break;

		case 'n':
			if (strcmp(optarg, ".") == 0)
				maxsessions = -1;
			else {
				maxsessions = strtol(optarg, &s, 10);
				if (*s != '\0')
					usage(argv0);
			}
			break;

		case 'e':
			exec = strdup(optarg);
			break;

		case 'l':
			nocopy++;
			break;

		case 'c':
			cwd = strdup(optarg);
			break;

		case 's':
			copyshared = 1;
			break;

		case 'S':
			copyshared = 0;
			break;

		case 'a':
			allflag++;
			break;

		case 'L':
			linebuf = 1;
			break;

		case 'p':
			shownode = 1;
			linebuf = 1;
			break;

		case 'm':
			spc_msize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage(argv0);
			break;

		case 'k':
			keypath = optarg;
			break;

		default:
			usage(argv0);
		}
	}

	user = sp_unix_users->uid2user(sp_unix_users, geteuid());
	if (!user)
		goto error;

	if(!keypath) {
		homepath = getenv("HOME");
		if(!homepath) {
			fprintf(stderr, "error: can not find home directory for id_rsa file, use -k");
			usage(argv0);
		}
		asprintf(&keypath, "%s/.ssh/id_rsa", homepath);
	}
	ukey = xauth_privkey_create(keypath);
	if (!ukey)
		goto error;

	if(exec && nocopy) {
		fprintf(stderr, "error: can not have both -l and -e\n");
		usage(argv0);
	}

	if (optind >= argc)
		usage(argv0);

	sysroot = getenv("XCPU_SYSROOT");
	if (!sysroot)
		sysroot = "";

	thisarch = getarch();
	if (nocopy && !cwd && getcwd(buf, sizeof(buf)) >= 0)
		cwd = strdup(buf);
		
	if(!allflag)
		ndstr = argv[optind++];

	if (optind >= argc)
		usage(argv0);
	if (nocopy) {
		copypath = NULL;
		exec = argv[optind++];
	} else {
		copypath = argv[optind++];
		if (!exec) {
			exec = strrchr(copypath, '/');
			if (exec)
				exec++;
			else
				exec = copypath;
		}
	}

	xargv = get_argv(exec, &argv[optind], argc - optind);
	if (!xargv)
		goto error;

	if (allflag) {
		char statserver[32];
		nds = xp_nodeset_list(NULL);
		sprintf(statserver, "localhost!%d", STAT_PORT);
		if(nds == NULL)
			nds = xp_nodeset_list(statserver);
		if (nds != NULL) {
			Xpnodeset *nds2 = xp_nodeset_create();
			if (nds2 != NULL) {
				if (xp_nodeset_filter_by_state(nds2, nds, "up") >= 0) {
					free(nds);
					nds = nds2;
				} else {
					free(nds2);
				}
			} /* if filter is unsuccessful just use the full set */
		}
	} else {
		nds = xp_nodeset_from_string(ndstr);
	}

	if (!nds)
		goto error;

	if (nds->len <= 0) {
		sp_werror("attempt to execute command to zero nodes", EIO);
		goto error;
	}

	cmd = xp_command_create(nds, user, ukey);
	if (!cmd)
		goto error;

	if (xp_session_get_localaddr(cmd->sessions->sessions[0], buf, sizeof(buf)) < 0)
		addr = NULL;
	else
		addr = buf;

	if (pmisetup(addr, nds->len, pmidebug) < 0)
		goto error;

	env = getenv("XCPUENV");
	if (env)
		env = process(env);

	elen = (env?strlen(env):0) + 512;
	e = sp_malloc(elen);
	snprintf(e, elen, "%s\nPMI_PORT=%s\nPMI_ID=%d\n", env?env:"", getpmiaddr(), 1);
	free(env);
	env = e;

	ncmds = xp_command_split_by_arch(cmd, &xcmds);
	if (ncmds < 0)
		goto error;

	for(lstart=1, i=0; i < ncmds; i++) {
		arch = xcmds[i]->nodes->nodes[0].arch;
		snprintf(archroot, sizeof(archroot), "%s/%s", sysroot, arch);
		if (stat(archroot, &st) < 0) {
			if (strcmp(arch, thisarch) == 0)
				archroot[0] = '\0';
			else {
				sp_werror("cannot find system root for architecture %s", ENOENT, arch);
				goto error;
			}
		}

		if (maxsessions < 0 || maxsessions > nds->len) {
			n = (int) sqrt(xcmds[i]->nodes->len);
			if (n*n < xcmds[i]->nodes->len)
				n++;
		} else
			n = maxsessions;

		xcmds[i]->nspawn = n;

		if (linebuf)
			xcmds[i]->flags = LineOut;
		else
			xcmds[i]->flags = 0;

		if (copyshared)
			xcmds[i]->flags |= CopyShlib;
		
		if(nocopy)
			xcmds[i]->flags |= NoCopy;

		if (env)
			xcmds[i]->env = strdup(env);

		if (copypath) {
			xcmds[i]->copypath = fullpath(copypath, archroot);
			if (!xcmds[i]->copypath) {
				sp_werror("bad command name: no file \"%s\" in $PATH", EIO, copypath);
				goto error;
			}
		}

		xcmds[i]->cwd = cwd;
		xcmds[i]->exec = strdup(exec);
		xcmds[i]->argv = strdup(xargv);
		xcmds[i]->stdout_cb = xmp_stdout;
		xcmds[i]->stderr_cb = xmp_stderr;
		xcmds[i]->wait_cb = xmp_wait;
		xcmds[i]->jobid = jobid;
		xcmds[i]->lidstart = lstart;

		xcmds[i]->sysroot = strdup(archroot);
		if (xp_command_exec(xcmds[i]) < 0)
			goto error;

		lstart += xcmds[i]->nodes->len;
	}

	setup_stdin_proc(0);
	signal(SIGTERM, pass_signal);
	signal(SIGINT, pass_signal);
	signal(SIGQUIT, pass_signal);
	signal(SIGKILL, pass_signal);
	signal(SIGHUP, pass_signal);

	if (xp_commands_wait(ncmds, xcmds) < 0)
		goto error;

	for(i = 0; i < ncmds; i++)
		xp_command_destroy(xcmds[i]);

	signal(SIGTERM, SIG_DFL);
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);
	signal(SIGKILL, SIG_DFL);
	signal(SIGHUP, SIG_DFL);
	close(xmp_inpipe[0]);
	if (xmp_inpid > 0)
		kill(xmp_inpid, SIGTERM);

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return 1;
}

void
xmp_stdout(Xpsession *s, u8 *buf, u32 buflen)
{
	Xpnode *nd;
	char *name;

	if (shownode) {
		nd = xp_session_get_node(s);
		if (nd)
			name = nd->name;
		else
			name = "??";

		write(1, name, strlen(name));
		write(1, ": ", 2);
	}

	write(1, buf, buflen);
}

void
xmp_stderr(Xpsession *s, u8 *buf, u32 buflen)
{
	Xpnode *nd;
	char *name;

	if (shownode) {
		nd = xp_session_get_node(s);
		if (nd)
			name = nd->name;
		else
			name = "??";

		write(2, name, strlen(name));
		write(2, ": ", 2);
	}

	write(2, buf, buflen);
}

void
xmp_wait(Xpsession *s, u8 *buf, u32 buflen)
{
	Xpnode *nd;

	nd = xp_session_get_node(s);
}

void
xmp_stdin(void)
{
	int i, n, err, ecode;
	char buf[8192];
	char *ename;

	if (!xmp_inspfd)
		return;

	n = 1;
	if (spfd_can_read(xmp_inspfd)) {
		while ((n = spfd_read(xmp_inspfd, buf, sizeof(buf))) > 0) {
			for(i = 0; i < ncmds; i++)
				xp_command_send(xcmds[i], (u8 *) buf, n);
		}
		if (n < 0) {
			/* this one is tricky, spfd_read can return -1 if the 
			 error read(2) is EAGAIN, but doesn't set ecode to EAGAIN */
			sp_rerror(&ename, &ecode);
			if (!ecode)
				n = 1;
		}
	}

	err = spfd_has_error(xmp_inspfd);
	if (n<=0 || err) {
		spfd_remove(xmp_inspfd);
		xmp_inspfd = NULL;
		for(i = 0; i < ncmds; i++)
			xp_command_close_stdin(xcmds[i]);
	}
}

static void
stdin_notify(Spfd *spfd, void *a)
{
	xmp_stdin();
}

static int
setup_stdin_proc(int infd)
{
	int n;
	char buf[8192];

	if (pipe(xmp_inpipe) < 0) {
		sp_uerror(errno);
		return -1;
	}

	xmp_inspfd = spfd_add(xmp_inpipe[0], stdin_notify, NULL);
	xmp_inpid = fork();
	if (xmp_inpid < 0) {
		sp_uerror(errno);
		return -1;
	}

	if (xmp_inpid) {
		/* parent */
		close(xmp_inpipe[1]);
		return 0;
	}

	/* child */
	close(xmp_inpipe[0]);

	while ((n = read(infd, buf, sizeof(buf))) > 0)
 		write(xmp_inpipe[1], buf, n);

	exit(0);
}
