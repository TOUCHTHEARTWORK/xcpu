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

#define _GNU_SOURCE

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
#include <sys/param.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <math.h>


#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpu.h"

extern int spc_chatty;

int signo;
static Spfd *xrx_inspfd;
int xrx_inbuflen;
char xrx_inbuf[8192];
int xrx_inpipe[2];
pid_t xrx_inpid;
int ncmds;
Xpcommand **xcmds;
int shownode;

static int setup_stdin_proc(int infd);
void xrx_stdout(Xpsession *s, u8 *buf, u32 buflen);
void xrx_stderr(Xpsession *s, u8 *buf, u32 buflen);
void xrx_wait(Xpsession *s, u8 *buf, u32 buflen);

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
	int lstart, attach, linebuf;
	char *ename, *s, *ndstr, *copypath, *xargv;
	char *env, *exec, *jobid, *sysroot, *thisarch, *arch;
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
	while ((c = getopt(argc, argv, "+dn:e:j:c:J:lsSaLpm:k:")) != -1) {
		switch (c) {
		case 'd':
			spc_chatty = 1;
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

		case 'j':
			jobid = strdup(optarg);
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

		case 'J':
			jobid = strdup(optarg);
			attach = 1;
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
		
	env = getenv("XCPUENV");
	if (env)
		env = process(env);

	if(!allflag && ((ndstr = getenv("NODES")) == NULL))
		ndstr = argv[optind++];

	if (!attach) {
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
	}

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

	if (attach) {
		ncmds = 1;
		cmd = xp_command_by_jobid(nds, jobid);
		if (!cmd)
			goto error;

		xcmds = sp_malloc(sizeof(Xpcommand *));
		if (!xcmds) 
			goto error;

		xcmds[0] = cmd;
		xcmds[0]->stdout_cb = xrx_stdout;
		xcmds[0]->stderr_cb = xrx_stderr;
		xcmds[0]->wait_cb = xrx_wait;
	} else {
		cmd = xp_command_create(nds, user, ukey);
		if (!cmd)
			goto error;

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
			xcmds[i]->stdout_cb = xrx_stdout;
			xcmds[i]->stderr_cb = xrx_stderr;
			xcmds[i]->wait_cb = xrx_wait;
			if (!jobid)
				if ((jobid = getenv("JOBID")) != NULL)
					jobid = strdup(jobid);

			xcmds[i]->jobid = jobid;
			xcmds[i]->lidstart = lstart;

			xcmds[i]->sysroot = strdup(archroot);
			if (xp_command_exec(xcmds[i]) < 0)
				goto error;

			lstart += xcmds[i]->nodes->len;
		}
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
	close(xrx_inpipe[0]);
	if (xrx_inpid > 0)
		kill(xrx_inpid, SIGTERM);

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return 1;
}

void
xrx_stdout(Xpsession *s, u8 *buf, u32 buflen)
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
xrx_stderr(Xpsession *s, u8 *buf, u32 buflen)
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
xrx_wait(Xpsession *s, u8 *buf, u32 buflen)
{
	Xpnode *nd;

	nd = xp_session_get_node(s);
}

void
xrx_stdin(void)
{
	int i, n, err, ecode;
	char buf[8192];
	char *ename;

	if (!xrx_inspfd)
		return;

	n = 1;
	if (spfd_can_read(xrx_inspfd)) {
		while ((n = spfd_read(xrx_inspfd, buf, sizeof(buf))) > 0) {
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

	err = spfd_has_error(xrx_inspfd);
	if (n<=0 || err) {
		spfd_remove(xrx_inspfd);
		xrx_inspfd = NULL;
		for(i = 0; i < ncmds; i++)
			xp_command_close_stdin(xcmds[i]);
	}
}

static void
stdin_notify(Spfd *spfd, void *a)
{
	xrx_stdin();
}

static int
setup_stdin_proc(int infd)
{
	int n;
	char buf[8192];

	if (pipe(xrx_inpipe) < 0) {
		sp_uerror(errno);
		return -1;
	}

	xrx_inspfd = spfd_add(xrx_inpipe[0], stdin_notify, NULL);
	xrx_inpid = fork();
	if (xrx_inpid < 0) {
		sp_uerror(errno);
		return -1;
	}

	if (xrx_inpid) {
		/* parent */
		close(xrx_inpipe[1]);
		return 0;
	}

	/* child */
	close(xrx_inpipe[0]);

	while ((n = read(infd, buf, sizeof(buf))) > 0)
 		write(xrx_inpipe[1], buf, n);

	exit(0);
}
