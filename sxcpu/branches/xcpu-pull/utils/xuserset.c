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
usage(char *name)
{
	fprintf(stderr, "usage: %s [-h] [-A admin_keyfile] {-a | nodeset} user uid group key\n", name);
	fprintf(stderr, "where: \n");
	fprintf(stderr, "\t-h prints this message\n");
	fprintf(stderr, "\tnodeset is the set of nodes to issue the command to\n");
	fprintf(stderr, "\tif -a is used, contact statfs and get a list of all nodes that are up\n");
	fprintf(stderr, "\tuser is the user name of the user\n");
	fprintf(stderr, "\tuid is the numeric user id of the user on the remote machine\n");
	fprintf(stderr, "\tgroup is the group this user belongs to\n");
	fprintf(stderr, "\tkey is the public key of that user (usually id_rsa.pub)\n");
	fprintf(stderr, "\n\tadmin_keyfile defaults to /etc/xcpu/admin_key\n");

	fprintf(stderr, "\nexample: %s 192.168.19.2 root 0 xcpu-admin ~/.ssh/id_rsa.pub\n", name);
	fprintf(stderr, "note: group should be created with xgroupset before xuserset is executed\n");
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
	if (n < 0)
		return n;

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
	int c, fd, n, ecode;
	char *ename, *s;
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

	if (argc < 6)
		usage(argv[0]);

	if (! allflag)
		nodeset = argv[optind++];
	username = argv[optind++];
	userid = strtol(argv[optind++], NULL, 10);
	groupname = argv[optind++];
	fd = open(argv[optind], O_RDONLY);
	if (fd < 0) {
		sp_suerror(argv[optind-1], errno);
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

	if (xp_nodeset_iterate(nds, setuser, NULL) < 0)
		goto error;

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s: Error: %s\n", argv[0], ename);
	return 1;
}

