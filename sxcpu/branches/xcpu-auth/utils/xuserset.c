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

static char *username;
static u32 userid;
static char *groupname;
static Xkey *adminkey;
static Spuser adminuser = {
	.uname = "xcpu-admin",
	.uid = 65530,
};
static char userkey[4096];

void
usage()
{
	fprintf(stderr, "usage:xuserset nodeset username uid group keyfile\n");
	fprintf(stderr, "E.g. xuserset  192.168.19.2 root 0 xcpu-admin  ~/.ssh/id_rsa.pub\n");
	fprintf(stderr, "It is useful to do an xgroupset first to get groups\n");
	exit(1);
}

static int
setuser(Xpnode *nd, void *cba)
{
	int n, buflen, ret;
	char buf[4096], *qkey;
	Spcfsys *fs;
	Spcfid *fid;
	Spwstat wst;

	fs = xp_node_mount(nd, &adminuser, adminkey);
	if (!fs)
		return -1;

	qkey = quotestrdup(userkey);
	snprintf(buf, sizeof(buf), "user-add %s %d %s %s\n", username, userid, groupname, qkey);
	free(qkey);
	buflen = strlen(buf);

	fid = spc_open(fs, "ctl", Owrite);
	if (!fid) {
		spc_umount(fs);
		return -1;
	}
	n = spc_write(fid, (u8 *) buf, buflen, 0);
	spc_close(fid);
	if (n < 0) {
		spc_umount(fs);
		return -1;
	}

	wst.type = ~0;
	wst.dev = ~0;
	wst.qid.type = ~0;
	wst.qid.version = ~0;
	wst.qid.path = ~0;
	wst.mode = ~0;
	wst.atime = ~0;
	wst.mtime = ~0;
	wst.name = NULL;
	wst.length = ~0;
	wst.uid = username;
	wst.gid = groupname;
	wst.muid = NULL;
	wst.extension = NULL;
	wst.n_uid = userid;
	wst.n_gid = ~0;
	wst.n_muid = ~0;

	ret = spc_wstat(fs, "clone", &wst);
	spc_umount(fs);
	return ret;
}

int
main(int argc, char **argv)
{
	int fd, n, ecode;
	char *ename, *s;
	struct passwd *pw;
	Xpnodeset *nds;

	if (argc < 6)
		usage();

	username = argv[2];
	userid = strtol(argv[3], NULL, 10);
	groupname = argv[4];
	fd = open(argv[5], O_RDONLY);
	if (fd < 0) {
		sp_suerror(argv[4], errno);
		goto error;
	}

	n = read(fd, userkey, sizeof(userkey) - 1);
	if (n < 0) {
		sp_uerror(errno);
		goto error;
	}
	s = strchr(userkey, '\n');
	if (s)
		*s = '\0';
	else
		userkey[n] = '\0';

	close(fd);
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

	if (xp_nodeset_iterate(nds, setuser, NULL) < 0)
		goto error;

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return 1;
}

