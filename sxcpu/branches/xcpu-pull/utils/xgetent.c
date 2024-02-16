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

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpu.h"

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

void usage() {
	fprintf(stderr, "usage: xgetent [-dap] <passwd|group> host,...\n");
	exit(1);
}

int read_pwent(Xpnode *node) {
	Spcfsys *fs;
	Spcfid *fid;
	char buf[8192];
	int n, off, ret;

	ret = 0;
	fs = xp_node_mount(node, user, ukey);
	if (!fs)
		return -1;

	fid = spc_open(fs, "pwent", Oread);
	if (!fid) {
		spc_umount(fs);
		return -1;
	}
	
	off = 0;
	while ((n = spc_read(fid, (u8 *) buf, sizeof(buf), off)) > 0) {
		printf("%s", buf);
		off += n;
	}
	
	if (n < 0)
		ret = -1;
	
	spc_close(fid);
	spc_umount(fs);
	return ret;
}
int read_grent(Xpnode *node) {
	Spcfsys *fs;
	Spcfid *fid;
	char buf[8192];
	int n, off, ret;

	ret = 0;
	fs = xp_node_mount(node, user, ukey);
	if (!fs)
		return -1;

	fid = spc_open(fs, "grent", Oread);
	if (!fid) {
		spc_umount(fs);
		return -1;
	}
	
	off = 0;
	while ((n = spc_read(fid, (u8 *) buf, sizeof(buf), off)) > 0) {
		printf("%s", buf);
		off += n;
	}

	if (n < 0)
		ret = -1;
	
	spc_close(fid);
	spc_umount(fs);
	return ret;
}

int
main(int argc, char **argv)
{
	int i, c, ecode, rc;
	int allflag = 0;
	char *ename, db[7];
	Xpnodeset *nds, *nds2;
	int port = STAT_PORT;
	char *end;

	while ((c = getopt(argc, argv, "+dap:")) != -1) {
		switch (c) {
		case 'd':
			spc_chatty = 1;
			break;

		case 'a':
			allflag++;
			break;
			
		case 'p':
			port = strtol(optarg, &end, 10);
			if (*end != '\0')
				usage();
			break;
		default:
			usage();
		}
	}

	if ((!allflag && argc - optind != 2 ) || (allflag && argc - optind != 1)) 
		usage();

	if (allflag) {
		char statserver[32];
		sprintf(statserver, "localhost!%d", port);
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

		snprintf(db, 7 * sizeof(char), "%s", argv[optind++]);
		if (strcmp("passwd", db) && strcmp("group", db))
		    usage();

		nds = xp_nodeset_from_string(argv[optind++]);
	}

	if (!nds)
		goto error;

	if (init_user() < 0)
		goto error;
	
	rc = 0;
	printf("\n");
	for (i=0; i < nds->len; i++) {
		if (!strcmp("passwd", db)) {
			printf("Password Database From Node: %s\n", 
			       nds->nodes[i].name);
			rc = read_pwent(&nds->nodes[i]);
		}
		else  {
			printf("Group Database From Node: %s\n", 
			       nds->nodes[i].name);
			rc = read_grent(&nds->nodes[i]);
		}
		
		printf("\n");
		if (rc)
			goto error;
	}

	exit(0);

 error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s: %d\n", ename, ecode);
	return 1;
}
