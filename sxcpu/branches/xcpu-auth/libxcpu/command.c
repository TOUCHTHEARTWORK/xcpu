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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
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
#include <regex.h>
#include <math.h>
#include <pwd.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpuimpl.h"

static void xp_command_finish_copy(Xpcopy *cp, void *aux);
static void xp_command_wait_recv(Spcfd *spcfd, void *aux);
static void xp_command_stdout_recv(Spcfd *spcfd, void *aux);
static void xp_command_stderr_recv(Spcfd *spcfd, void *aux);
static void xp_command_stdin_send(Spcfd *spcfd, void *aux);
static int xp_command_do_kill(Xpcommand *cm);

static Xpcommand *
xp_command_alloc(void)
{
	Xpcommand *cm;

	cm = sp_malloc(sizeof(*cm));
	if (!cm)
		return NULL;

	cm->sessions = NULL;
	cm->flags = LineOut;
	cm->nodes = NULL;
	cm->copypath = NULL;
	cm->exec = NULL;
	cm->cwd = NULL;
	cm->argv = NULL;
	cm->env = NULL;
	cm->jobid = NULL;
	cm->lidstart = 1;
	cm->stdout_cb = NULL;
	cm->stderr_cb = NULL;
	cm->wait_cb = NULL;
	cm->ename = NULL;
	cm->ecode = 0;
	cm->signo = -1;
	cm->sysroot = NULL;
	cm->user = NULL;
	cm->userkey = NULL;

	return cm;
}

Xpcommand *
xp_command_create(Xpnodeset *nds, Spuser *user, Xkey *ukey)
{
	Xpcommand *cm;

	cm = xp_command_alloc();
	if (!cm)
		return NULL;

	cm->user = user;
	cm->userkey = ukey;
	cm->nodes = nds;
	cm->sessions = xp_sessionset_create(nds, cm);
	if (!cm->sessions) {
		free(cm);
		return NULL;
	}

	return cm;
}

void 
xp_command_destroy(Xpcommand *cm)
{
	if (cm->sessions)
		xp_sessionset_destroy(cm->sessions);

	free(cm->env);
	free(cm->argv);
	free(cm->exec);
	free(cm->copypath);
	free(cm->jobid);
	free(cm);
}

Xpcommand *
xp_command_by_jobid(Xpnodeset *nds, char *jobid)
{
	Xpcommand *cm;

	cm = xp_command_alloc();
	if (!cm)
		return NULL;

	cm->sessions = xp_sessionset_by_jobid(nds, jobid, cm);
	if (!cm->sessions) {
		free(cm);
		return NULL;
	}

	cm->nodes = nds;
	return cm;
}

int
xp_command_exec(Xpcommand *cm)
{
	int i, j, k, n, m, nnodes;
	int b, e, cn, ce;
	u64 l;
	char *buf, *str, *t, **deps;
	struct stat st;
	Xpsession **ss;

	buf = NULL;

	if (!cm->jobid)
		cm->jobid = strdup("");

	if (!cm->userkey) {
		cm->userkey = xauth_user_pubkey();
		if (!cm->userkey)
			return -1;
	}

	if (!(cm->flags & NoCopy)) {
		if(stat(cm->copypath, &st) < 0) {
			sp_uerror(errno);
			return -1;
		}
		if (S_ISREG(st.st_mode) && cm->flags & CopyShlib) {
			n = xp_ldd(cm->copypath, cm->sysroot, &deps);
			if (n < 0)
			return -1;
		} else {
			deps = sp_malloc(2*sizeof(char *));
			deps[0] = cm->copypath;
			deps[1] = NULL;
		}
	} else {
		deps = NULL;
	}

	nnodes = cm->sessions->len;
	ss = cm->sessions->sessions;
	if (cm->nspawn > 0) {
		n = cm->nspawn;
		if (n > nnodes)
			n = nnodes;
		m = nnodes/n + (nnodes%n?1:0);
	} else {
		n = nnodes;
		m = 1;
	}

	cm->tcnt = n;
	/* n is the number of sessions that we copy the files to,
	   m is the number of sessions that each node will clone itself to */
	if (m > 1)
		cn = (nnodes - n)/(m - 1) + ((nnodes-n)%(m-1)?1:0);
	else
		cn = 0;

	ce = nnodes - n + cn;
	if (ce > nnodes)
		ce = nnodes;

	i = 0;
	while (i < ce) {
		b = i + 1;
		e = i + m;

		if (e > ce)
			e = ce;

		str = NULL;
		if (b < e) {
			for(j = b; j < e; j++) {
				if (xp_session_get_passkey(ss[j]) < 0)
					goto error;
			}

			k = 0;
			for(j = b; j < e; j++)
				k += strlen(ss[j]->node->addr) + strlen(ss[j]->sid) + 
					ss[j]->pklen + 3;

			k += 32;	
			str = sp_malloc(k);
                	if (!str)
                        	goto error;

			sprintf(str, "clone %d ", cm->nspawn);
			t = str + strlen(str);
                	for(j = b; j < e; j++) {
				k = strlen(ss[j]->node->addr);
				memmove(t, ss[j]->node->addr, k);
				t += k;
				*(t++) = '/';
				k = strlen(ss[j]->sid);
				memmove(t, ss[j]->sid, k);
				t += k;
				*(t++) = '/';
				memmove(t, ss[j]->pk, ss[j]->pklen);
				t += ss[j]->pklen;
				*(t++) = ',';
	                }

			*(t++) = '\n';
			*t = '\0';
		}

		k = xp_session_setup_start(ss[i], cm->env, cm->argv, deps, str);
		free(str);
		if (k < 0)
			goto error;

		ss[i]->copyf->finish = xp_command_finish_copy;
		ss[i]->copyf->finishaux = cm;

		i = e;
	}

	while (i < nnodes) {
		k = xp_session_setup_start(ss[i], cm->env, cm->argv, deps, NULL);
		if (k < 0)
			goto error;

		ss[i]->copyf->finish = xp_command_finish_copy;
		ss[i]->copyf->finishaux = cm;
		i++;
	}


	while (!cm->ename && cm->tcnt>0)
		sp_poll_once();

	for(i = 0; i < nnodes; i++) 
		if (ss[i]->copyf)
			xp_session_setup_finish(ss[i]);

	if (cm->ename) {
		sp_werror(cm->ename, cm->ecode);
		goto error;
	}

	buf = sp_malloc(strlen(cm->jobid) + strlen(cm->exec) + (cm->cwd?strlen(cm->cwd):0) + 64);
	if (!buf)
		goto error;

	k = strlen(buf);
	for(l = 0, i = 0; i < nnodes; i++) {
		sprintf(buf, "id %s/%d\nexec %s %s\n", cm->jobid, cm->lidstart + i, 
			cm->exec, cm->cwd?cm->cwd:"");
		k = strlen(buf);
		j = spc_write(ss[i]->ctl, (u8 *) buf, k, l);
		if (j < 0)
			goto error;
		else if (j != k) {
			sp_werror("error while executing", EIO);
			goto error;
		}

		l += j;
	}

	free(deps);
	free(buf);
	return 0;

error:
	free(deps);
	free(buf);
	return -1;	
}

static void
xp_command_finish_copy(Xpcopy *cp, void *aux)
{
	int ecode;
	char *ename;
	Xpcommand *cm;

	cm = aux;
	sp_rerror(&ename, &ecode);
	if (ename) {
		cm->ecode = ecode;
		cm->ename = strdup(ename);	/* TODO: handle NULL result */
	}

	cm->tcnt--;
}

int
xp_command_wait(Xpcommand *cm)
{
	return xp_commands_wait(1, &cm);
}

int
xp_commands_wait(int ncmds, Xpcommand **cms)
{
	int i, n, ns, done;
	Xpsession **ss;

	for(n = 0; n < ncmds; n++) {
		ns = cms[n]->sessions->len;
		ss = cms[n]->sessions->sessions;

		cms[n]->tcnt = ns;
		for(i = 0; i < ns; i++) {
			ss[i]->wspcfd = spcfd_add(ss[i]->wait, xp_command_wait_recv, ss[i]);
			if (!ss[i]->wspcfd)
				goto error;

			ss[i]->ispcfd = spcfd_add(ss[i]->in, xp_command_stdin_send, ss[i]);
			if (!ss[i]->ispcfd)
				goto error;

			if (cms[n]->stdout_cb) {
				ss[i]->ospcfd = spcfd_add(ss[i]->out, xp_command_stdout_recv, ss[i]);
				if (!ss[i]->ospcfd)
					goto error;
			}

			if (cms[n]->stderr_cb) {
				ss[i]->espcfd = spcfd_add(ss[i]->err, xp_command_stderr_recv, ss[i]);
				if (!ss[i]->espcfd)
					goto error;
			}
		}
	}

	while (1) {
		done = 1;
		for(n = 0; n < ncmds; n++) {
			if (cms[n]->ename) 
				break;

			if (cms[n]->signo > 0) {
				xp_command_do_kill(cms[n]);
				cms[n]->signo = -1;
			}

			if (cms[n]->tcnt > 0)
				done = 0;
		}

		if (n < ncmds) {
			sp_werror(cms[n]->ename, cms[n]->ecode);
			goto error;
		}

		if (done)
			break;

		sp_poll_once();
	}

        return 0;

error:
	return -1;
}

static void
xp_command_seterror(Xpcommand *cm)
{
	int ecode;
	char *ename;

	sp_rerror(&ename, &ecode);
	if (ename != NULL) {
		cm->ename = strdup(ename);
		cm->ecode = ecode;
	}
}

static void
xp_command_wait_recv(Spcfd *spcfd, void *aux)
{
	int n, m;
	char *t;
	Xpsession *s;
	Xpcommand *cm;
	char buf[32];

	s = aux;
	cm = s->command;

	if (spcfd_has_error(spcfd)) {
		sp_werror("error while reading", EIO);
		xp_command_seterror(cm);
		spcfd_remove(s->wspcfd);
		s->wspcfd = NULL;
		return;
	}

	while (spcfd_can_read(spcfd)) {
		n = spcfd_read(spcfd, buf, sizeof(buf));
		if (n > 0) {
			if (s->exitcode)
				m = strlen(s->exitcode);
			else
				m = 0;

			t = realloc(s->exitcode, m + n + 1);
			if (!t) {
				sp_werror(Enomem, ENOMEM);
				xp_command_seterror(cm);
				return;
			}

			s->exitcode = t;
			memmove(s->exitcode + m, buf, n);
			s->exitcode[n+m] = '\0';
		} else if (n < 0) {
			xp_command_seterror(cm);
			spcfd_remove(s->wspcfd);
			s->wspcfd = NULL;
			return;
		} else { /* n == 0 */
			spcfd_remove(s->wspcfd);
			s->wspcfd = NULL;
			if (cm->wait_cb)
				(*cm->wait_cb)(s, (u8 *) s->exitcode, strlen(s->exitcode));

			if (!s->wspcfd && !s->ospcfd && !s->espcfd)
				cm->tcnt--;
			return;
		}
	}
}

static void
xp_command_stdout_recv(Spcfd *spcfd, void *aux)
{
	int n;
	char *p, *t;
	Xpsession *s;
	Xpcommand *cm;

	s = aux;
	cm = s->command;
	if (spcfd_has_error(spcfd)) {
		sp_werror("error while reading", EIO);
		xp_command_seterror(cm);
		spcfd_remove(s->ospcfd);
		s->ospcfd = NULL;
		return;
	}

	while (spcfd_can_read(spcfd)) {
		n = spcfd_read(spcfd, s->outbuf + s->outpos, sizeof(s->outbuf) - s->outpos);
		if (n <= 0) {
			if (n < 0)
				xp_command_seterror(cm);

			spcfd_remove(s->ospcfd);
			s->ospcfd = NULL;

			if (!s->wspcfd && !s->ospcfd && !s->espcfd)
				cm->tcnt--;
			return;
		}

		s->outpos += n;
		if (cm->flags & LineOut) {
			p = s->outbuf;
			while ((t = memchr(p, '\n', s->outpos - (p - s->outbuf))) != NULL) {
				(*cm->stdout_cb)(s, (u8 *) p, t - p + 1);
				p = t + 1;
			}

			if (p != s->outbuf) {
				s->outpos -= p - s->outbuf;
				memmove(s->outbuf, p, s->outpos);
			}

			if (s->outpos == sizeof(s->outbuf)) {
				(*cm->stdout_cb)(s, (u8 *) s->outbuf, s->outpos);
				s->outpos = 0;
			}
		} else {
			(*cm->stdout_cb)(s, (u8 *) s->outbuf, s->outpos);
			s->outpos = 0;
		}
	}
}

static void
xp_command_stderr_recv(Spcfd *spcfd, void *aux)
{
	int n;
	char *p, *t;
	Xpsession *s;
	Xpcommand *cm;

	s = aux;
	cm = s->command;
	if (spcfd_has_error(spcfd)) {
		sp_werror("error while reading", EIO);
		xp_command_seterror(cm);
		spcfd_remove(s->espcfd);
		s->espcfd = NULL;
		return;
	}

	while (spcfd_can_read(spcfd)) {
		n = spcfd_read(spcfd, s->errbuf + s->errpos, sizeof(s->errbuf) - s->errpos);
		if (n <= 0) {
			if (n < 0)
				xp_command_seterror(cm);

			spcfd_remove(s->espcfd);
			s->espcfd = NULL;
			if (!s->wspcfd && !s->ospcfd && !s->espcfd)
				cm->tcnt--;
			return;
		}

		s->errpos += n;
		if (cm->flags & LineOut) {
			p = s->errbuf;
			while ((t = memchr(p, '\n', s->errpos - (p - s->errbuf))) != NULL) {
				(*cm->stderr_cb)(s, (u8 *) p, t - p + 1);
				p = t + 1;
			}

			if (p != s->errbuf) {
				s->errpos -= p - s->errbuf;
				memmove(s->errbuf, p, s->errpos);
			}

			if (s->errpos == sizeof(s->errbuf)) {
				(*cm->stderr_cb)(s, (u8 *) s->errbuf, s->errpos);
				s->errpos = 0;
			}
		} else {
			(*cm->stderr_cb)(s, (u8 *) s->errbuf, s->errpos);
			s->errpos = 0;
		}
	}
}

static void
xp_command_stdin_send(Spcfd *spcfd, void *aux)
{
	int n;
	char *t;
	Xpsession *s;
	Xpcommand *cm;

	s = aux;
	cm = s->command;
	if (spcfd_has_error(spcfd)) {
		sp_werror("error while writing", EIO);
		goto error;
	}

	if (!spcfd_can_write(spcfd) || !s->inpos)
		return;

	n = spcfd_write(spcfd, s->inbuf, s->inpos);
	if (n <= 0) 
		goto error;

	s->inpos -= n;
	if (s->inpos)
		memmove(s->inbuf, s->inbuf + n, s->inpos);
	else if (s->closein)
		xp_session_close_stdin(s);

	if (s->insize + s->inpos > 64) {
		t = realloc(s->inbuf, s->inpos + 64);
		if (t) {
			s->insize = s->inpos + 64;
			s->inbuf = t;
		}
	}

	return;

error:
	xp_command_seterror(cm);
	spcfd_remove(s->ispcfd);
	s->ispcfd = NULL;
}

static int
xp_command_do_kill(Xpcommand *cm)
{
	int i, n, ns;
	u64 l;
	char buf[64];
	Xpsession **ss;

	ns = cm->sessions->len;
	ss = cm->sessions->sessions;
	snprintf(buf, sizeof(buf), "signal %d\n", cm->signo);

	/* TODO: can we do spc_write, or we have to use spcfd??? */
	for(l = 0, i = 0; i < ns; i++) {
		n = spc_write(ss[i]->ctl, (u8 *) buf, strlen(buf), l);
		if (n < 0)
			return -1;
		else if (n == 0 || n != strlen(buf)) {
			sp_werror("error while writing", EIO);
			return -1;
		}

		l += n;
	}

	return 0;
}

int
xp_command_kill(Xpcommand *cm, int signo)
{
	/* this is kinda tricky, the signal handler will interrupt 
	   sp_poll_once, so we don't have to do anything except 
	   to remember the signal number */
	if (!cm->flags&Wait) {
		sp_werror("command is not running", EIO);
		return -1;
	}

	cm->signo = signo;
	return 0;
}

int
xp_command_wipe(Xpcommand *cm)
{
	int i, n, ns;
	u64 l;
	char buf[64];
	Xpsession **ss;

	ns = cm->sessions->len;
	ss = cm->sessions->sessions;
	snprintf(buf, sizeof(buf), "wipe\n");

	/* TODO: can we do spc_write, or we have to use spcfd??? */
	for(l = 0, i = 0; i < ns; i++) {
		n = spc_write(ss[i]->ctl, (u8 *) buf, strlen(buf), l);
		if (n < 0)
			return -1;
		else if (n == 0 || n != strlen(buf)) {
			sp_werror("error while writing", EIO);
			return -1;
		}

		l += n;
	}

	return 0;
}

int
xp_command_send(Xpcommand *cm, u8 *data, u32 datalen)
{
	int i, n, m, ns;
	char *t;
	Xpsession **ss;

	ns = cm->sessions->len;
	ss = cm->sessions->sessions;

	for(i = 0; i < ns; i++) {
		if (!ss[i]->ispcfd) {
			sp_werror("error while writing", EIO);
			return -1;
		}

		n = ss[i]->inpos + datalen;
		if (n > ss[i]->insize) {
			m = n + (n%32?32:0);
			t = realloc(ss[i]->inbuf, m);
			if (!t) {
				sp_werror(Enomem, ENOMEM);
				return -1;
			}

			ss[i]->inbuf = t;
			ss[i]->insize = m;
		}

		memmove(ss[i]->inbuf + ss[i]->inpos, data, datalen);
		ss[i]->inpos += datalen;
		if (ss[i]->inpos == datalen)
			xp_command_stdin_send(ss[i]->ispcfd, ss[i]);
	}

	return 0;
}

int
xp_command_close_stdin(Xpcommand *cm)
{
	int i, ns;
	Xpsession **ss;

	ns = cm->sessions->len;
	ss = cm->sessions->sessions;
	for(i = 0; i < ns; i++)
		xp_session_close_stdin(ss[i]);

	return 0;
}

int
xp_command_split_by_arch(Xpcommand *cm, Xpcommand ***cms)
{
	int i, j, n;
	Xpsessionset **ssa;
	Xpcommand **ret;

	n = xp_sessionset_split_by_arch(cm->sessions, &ssa);
	if (n < 0)
		return -1;

	ret = sp_malloc(n * sizeof(Xpcommand *));
	if (!ret)
		return -1;

	for(i = 0; i < n; i++) {
		ret[i] = sp_malloc(sizeof(Xpcommand));
		if (!ret)
			return -1;

		memmove(ret[i], cm, sizeof(Xpcommand));
		ret[i]->sessions = ssa[i];
		ret[i]->nodes = xp_nodeset_create();
		for(j = 0; j < ssa[i]->len; j++) {
			ssa[i]->sessions[j]->command = ret[i];
			xp_nodeset_add(ret[i]->nodes, ssa[i]->sessions[j]->node);
		}

		if (cm->copypath)
			ret[i]->copypath = strdup(cm->copypath);
		if (cm->exec)
			ret[i]->exec = strdup(cm->exec);
		if (cm->cwd)
			ret[i]->cwd = strdup(cm->cwd);
		if (cm->argv)
			ret[i]->argv = strdup(cm->argv);
		if (cm->env)
			ret[i]->env = strdup(cm->env);
		if (cm->jobid)
			ret[i]->jobid = strdup(cm->jobid);
		if (cm->sysroot)
			ret[i]->sysroot = strdup(cm->sysroot);
	}

	*cms = ret;
	return n;
}
