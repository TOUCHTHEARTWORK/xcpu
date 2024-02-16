/*
 * Copyright (C) 2006 by Andrey Mirtchovski <andrey@lanl.gov>
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

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"

extern int spc_chatty;

static Spuser *user;
static Xkey *ukey;

static int
init_user(void)
{
	char *homepath, keypath[128];

	user = sp_unix_users->uid2user(sp_unix_users, geteuid());
	if (!user)
		return -1;

	homepath = getenv("HOME");
	snprintf(keypath, sizeof(keypath), "%s/.ssh/id_rsa", homepath);
	ukey = xauth_privkey_create(keypath);
	if (!ukey)
		return -1;

	return 0;
}

void 
usage(char *argv) {
	fprintf(stderr, "usage: %s [-dhj] sig host:<pid|jid> [host:<pid|jid> ...]\n", argv);
	exit(1);
}

int
main(int argc, char **argv)
{
	int i, c, n, ecode, killjob;
	int signal;
	char *ename;
	Xpnodeset *ns;
	Xpproc *procs, *xp;
	char *s;

	while ((c = getopt(argc, argv, "+dhj")) != -1) {
		switch (c) {
		case 'd':
			spc_chatty = 1;
			break;
		case 'j':
			killjob++;
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}

	if (argc - optind < 2)
		usage(argv[0]);

	signal = strtol(argv[optind++], &s, 10);
	if (*s != '\0') {
		fprintf(stderr, "bad signal argument %s: expected int", argv[optind-1]);
		usage(argv[0]);
	}

	if (init_user() < 0)
		goto error;

	for(;optind < argc; optind++) {
		int isjid = 0;
		int pid = -1;
		char *id;

		s = strchr(argv[optind], ':');
		if(s) {
			*s++ = '\0';
			ns = xp_nodeset_from_string(argv[optind]);
			id = s;
		} else {
			ns = xp_nodeset_list(NULL);	/* assume all nodes when no nodelist */
			if(ns == NULL)
				ns = xp_nodeset_list("localhost!20002");
			id = argv[optind];
		}
		if(ns == NULL) {
			fprintf(stderr, "can not obtain nodeset from statfs\n");
			goto error;
		}
		isjid = (strchr(id, '/') == NULL);
		if(!isjid) {
			pid = strtol(id, &s, 10);
			if(*s != '\0')
				fprintf(stderr, "bad process id: %s; skipping\n", id);
			continue;
		}

		n = xp_proc_list(ns, user, ukey, &procs);
		if (n < 0) {
			fprintf(stderr, "can not obtain process list for nodeset %s; skipping\n", argv[optind]);
			continue;
		}
		for(i = 0; i < n; i++) {
			xp = &procs[i];
			if(isjid) {
				if(!strcmp(xp->xcpujid, id))
					xp_proc_kill(xp, user, ukey, signal);
			} else {
				if(xp->pid == pid)
					xp_proc_kill(xp, user, ukey, signal);
			}
		}
	}

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s: %d\n", ename, ecode);
	return 1;
}

