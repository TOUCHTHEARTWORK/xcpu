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
#include "npfs.h"
#include "npclient.h"
#include "tree.h"

enum {
	Bufsize = 8192,
};

// static char *Enomem = "not enough memory";

void
treefree(Tfile *f)
{
	Tfile *c, *c1;

	c = f->dir;
	while (c) {
		c1 = c->next;
		treefree(c);
		c = c1;
	}

	free(f->de);
	free(f->data);
	free(f);
}

Tfile *
treealloc(char *file)
{
	Tfile *ret;

	ret = malloc(sizeof(*ret));
	if (!ret) {
		np_werror(Enomem, ENOMEM);
		return NULL;
	}

	ret->fs = NULL;
	ret->path = strdup(file);
	ret->data = NULL;
	ret->next = NULL;
	ret->dir = NULL;
	ret->de = NULL;

	return ret;
}
