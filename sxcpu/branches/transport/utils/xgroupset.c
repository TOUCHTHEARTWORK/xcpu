/*
 * Copyright (C) 2007 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <sys/param.h>
#include <signal.h>
#include <dirent.h>
#include <signal.h>
#include <regex.h>
#include <math.h>
#include <pwd.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpu.h"

extern int spc_chatty;

static char *groupname;
static u32 gid;
static char *groupname;
static Xkey *adminkey;
static Spuser adminuser = {
	.uname = "xcpu-admin",
	.uid = 65530,
};

void
usage(char *name)
{
	fprintf(stderr, "usage: %s [-h] [-A adminkey] {-a | nodeset} gname gid [gname gid ...]\n", name);
	exit(1);
}

static int
setgroup(Xpnode *nd, void *cba)
{
	int buflen, ret;
	char buf[4096]; /* on stack? FIX ME */
	Spcfsys *fs;
	Spcfid *fid;
	fs = xp_node_mount(nd, &adminuser, adminkey);
	if (!fs)
		return -1;

	snprintf(buf, sizeof(buf), "group-add %s %d\n", groupname, gid);
	buflen = strlen(buf);

	fid = spc_open(fs, "ctl", Owrite);
	if (!fid) {
		spc_umount(fs);
		return -1;
	}
	ret = spc_write(fid, (u8 *) buf, buflen, 0);
	if (ret < 0)
		return ret;

	spc_close(fid);
	return 0;
}

int
main(int argc, char **argv)
{
	int ecode, c;
	char *ename;
	char *nodeset;
	char *adminkeyfile = "/etc/xcpu/admin_key";
	struct passwd *pw;
	Xpnodeset *nds, *nds2;
	int allflag = 0;

	while((c = getopt(argc, argv, "aA:dh")) != -1) {
		switch(c) {
		case 'd':
			spc_chatty = 1;
			break;
		case 'A':
			adminkeyfile = strdup(optarg);
			break;
		case 'a':
			allflag++;
			break;
		case 'h':
		default:
			usage(argv[0]);
		}
	}

	if (argc < 4)
		usage(argv[0]);

	if (! allflag)
		nodeset = argv[optind++];
	
	groupname = argv[optind++];
	gid = strtol(argv[optind++], NULL, 10);
	adminkey = xauth_privkey_create(adminkeyfile);
	if (!adminkey)
		goto error;

	setpwent();
	while ((pw = getpwent()) != NULL) {
		if(!strcmp(pw->pw_name, adminuser.uname)) {
			adminuser.uid = pw->pw_uid;
			break;
		}
	}
	endpwent(); 


	if (allflag) {
		char statserver[32];
		sprintf(statserver, "localhost!%d", STAT_PORT);
		nds = xp_nodeset_list(NULL);
		if(nds == NULL)
			nds = xp_nodeset_list(statserver);
		if (nds != NULL) {
			nds2 = xp_nodeset_create();
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
		nds = xp_nodeset_from_string(nodeset);
	}

	if (!nds)
		goto error;

	if (xp_nodeset_iterate(nds, setgroup, NULL) < 0)
		goto error;

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s: Error: %s\n", argv[0], ename);
	return 1;
}
