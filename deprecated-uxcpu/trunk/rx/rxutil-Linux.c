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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
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

#include "npfs.h"
#include "strutil.h"
#include "npclient.h"
#include "copyfile.h"
#include "rx.h"

typedef struct Mntentry Mntentry;
struct Mntentry {
	char*		mntpt;
	char*		addr;
	Mntentry*	next;
};

static pthread_mutex_t mntptlock = PTHREAD_MUTEX_INITIALIZER;
static int mntptsloaded;
static Mntentry *mntpts;

static int
load_mntpts(void)
{
	int n;
	FILE *f;
	char buf[128];
	char *s, *t, **ts;
	Mntentry *mp;

	f = fopen("/proc/mounts", "r");
	if (!f) {
		np_uerror(errno);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f) != NULL) {
		n = tokenize(buf, &ts);
		if (n < 4) {
			np_werror("/proc/mounts: invalid mounts format", EIO);
			free(ts);
			return -1;
		}

		if (strcmp(ts[2], "9p") != 0) {
			free(ts);
			continue;
		}

		mp = malloc(sizeof(*mp));
		if (!mp) {
			free(ts);
			np_werror(Enomem, ENOMEM);
			return -1;
		}

		mp->mntpt = strdup(ts[1]);
		s = strstr(ts[3], "port=");
		if (!s) {
			np_werror("/proc/mounts: cannot find port= option", EIO);
			free(mp);
			free(ts);
			return -1;
		}

		n = strtol(s + 5, &t, 10);
		if (*t != '\0' && *t != ',') {
			np_werror("/proc/mounts: invalid option format", EIO);
			free(mp);
			free(ts);
			return -1;
		}

		mp->addr = malloc(strlen(ts[0]) + 32);
		if (!mp->addr) {
			np_werror(Enomem, ENOMEM);
			free(mp);
			free(ts);
			return -1;
		}

		sprintf(mp->addr, "tcp!%s!%d", ts[1], n);
		mp->next = mntpts;
		mntpts = mp;
		free(ts);
	}

	return 0;
}

char *
rxmntpt2addr(char *mntpt)
{
	Mntentry *mp;

	if (!mntptsloaded) {
		pthread_mutex_lock(&mntptlock);
		if (!mntptsloaded && load_mntpts()<0) {
				pthread_mutex_unlock(&mntptlock);
				return NULL;
		}
		pthread_mutex_unlock(&mntptlock);
	}

	for(mp = mntpts; mp != NULL; mp = mp->next)
		if (strcmp(mntpt, mp->mntpt) == 0) 
			return mp->addr;

	return NULL;
}
			
