/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <sys/mman.h>
#include <time.h>
#include <pthread.h>
#include "spfs.h"
#include "yspufs.h"

typedef struct Spuargs Spuargs;

struct Spuargs {
	spe_context_ptr_t	speid;
	void*			arg;
	void*			env;
	pthread_t		thread;
};

void
usage()
{
	fprintf(stderr, "npfs: -d -s -p port -w nthreads\n");
	exit(-1);
}

void *proc(void *a)
{
	int err;
	unsigned int entry;
	Spuargs *args;

	args = a;
	entry = SPE_DEFAULT_ENTRY;
	err = spe_context_run(args->speid, &entry, 0, args->arg, args->env, NULL);
	if (err < 0) {
		fprintf(stderr, "cannot run context\n");
		return NULL;
	}

	return NULL;
}

int
main(int argc, char **argv)
{
	int err, i, c, debuglevel, ecode;
	unsigned int entry;
	u64 arg;
	u64 env;
	Spuser *user;
	char *ename;
	spe_context_ptr_t speid;
	spe_gang_context_ptr_t gctx;
	spe_program_handle_t *spebin;
	Spsrv *srv;
	Spuargs *sargs;

	arg = 0;
	debuglevel = 0;
	user = sp_uid2user(getuid());
	while ((c = getopt(argc, argv, "du:b:a:")) != -1) {
		switch (c) {
		case 'a':
			arg = strtol(optarg, 0, 10);
			break;

		case 'd':
			debuglevel = 1;
			break;

		case 'u':
			user = sp_uname2user(optarg);
			break;

		default:
			fprintf(stderr, "invalid option\n");
		}
	}

	if (!user) {
		fprintf(stderr, "invalid user\n");
		return -1;
	}

	gctx = spe_gang_context_create(0);
	srv = rootfs_init(debuglevel);
	if (!srv)
		goto error;

	if (ramfs_init() < 0)
		goto error;
	if (npfs_init() < 0)
		goto error;

	if (logfs_init() < 0)
		goto error;

	if (ufs2_init() < 0)
		goto error;

	if (pipefs_init() < 0)
		goto error;

        if (optind >= argc)
                usage();

	sargs = malloc(sizeof(*sargs) * (argc - optind));
	for(i = 0; i+optind < argc; i++) {
		spebin = spe_image_open(argv[i + optind]);
		if (!spebin) {
			fprintf(stderr, "cannot load the spe binary\n");
			return 1;
		}

		env = ((u64)(argc - optind)) << 32 | i;
		speid = spe_context_create(SPE_CFG_SIGNOTIFY2_OR | SPE_EVENTS_ENABLE, gctx);
		if (!speid) {
			fprintf(stderr, "cannot create thread\n");
			return 1;
		}

		err = spe_program_load(speid, spebin);
		if (err < 0) {
			fprintf(stderr, "cannot load program\n");
			return 1;
		}

		if (!sp_cbeconn_create(srv, speid))
			goto error;

		sargs[i].speid = speid;
		sargs[i].arg = arg;
		sargs[i].env = env;

		pthread_create(&sargs[i].thread, NULL, proc, &sargs[i]);
	}

	rootfs_loop();

	return 0;

error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "Error: %s\n", ename);
	return -1;
}
