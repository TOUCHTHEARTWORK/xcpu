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
usage()
{
	fprintf(stderr, "usage: xgroupset nodeset gname gid [gname gid ...]\n");
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
	spc_close(fid);
	return ret;
}

int
main(int argc, char **argv)
{
	int ecode;
	char *ename;
	struct passwd *pw;
	Xpnodeset *nds;


	if (argc < 4)
		usage();

	groupname = argv[2];
	gid = strtol(argv[3], NULL, 10);
	adminkey = xauth_privkey_create("/etc/xcpu/admin_key");
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


	nds = xp_nodeset_from_string(argv[1]);
	if (!nds)
		goto error;

	if (xp_nodeset_iterate(nds, setgroup, NULL) < 0)
		goto error;

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return 1;
}

