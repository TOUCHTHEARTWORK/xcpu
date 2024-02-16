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
#include "xcpu.h"
#include "libxauth.h"
#include "libxcpu.h"
#include "xcpuimpl.h"

static int xp_auth(Spcfid *afid, Spuser *user, void *aux);

Spcfsys *
xp_node_mount(Xpnode *nd, Spuser *user, Xkey *key)
{
	if (!user) {
		user = sp_unix_users->uid2user(sp_unix_users, geteuid());
		if (!user) {
			sp_werror(Eunknownuser, EIO);
			return NULL;
		}
	}

	return spc_netmount(nd->addr, user, XCPU_PORT, xp_auth, key);
}

static int
xp_auth(Spcfid *afid, Spuser *user, void *aux)
{
	int n;
	char buf[4096], sig[4096];
	Xkey *key;

	key = aux;
	n = spc_read(afid, (u8 *) buf, sizeof(buf), 0);
	if (n < 0)
		return -1;
	else if (n == 0) {
		sp_werror("authentication failed", EIO);
		return -1;
	}

	n = xauth_sign((u8 *) buf, n, (u8 *) sig, sizeof(sig), key);
	if (n < 0)
		return -1;

	n = spc_write(afid, (u8 *) sig, n, 0);
	if (n < 0)
		return -1;

	return 0;
}
