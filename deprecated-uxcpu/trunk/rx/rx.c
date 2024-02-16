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
#include "npclient.h"
#include "strutil.h"
#include "copyfile.h"
#include "rx.h"

int use9p;
int maxsessions;
int nocopy;
char *exec;
char *copyfile;
char *args;
char *env;

int nnodes;
char **nodes;

extern int npc_chatty;

void usage() {
	fprintf(stderr, "usage ...\n");
	exit(1);
}

static void
setup_env(void)
{
	env = getenv("XCPUENV");
//	fprintf(stderr, "XCPUENV: %s\n", env);
}

static void
setup_nodes(char *nodelist)
{
	int i;
	char *s, *dname;
	DIR *dir;
	struct dirent *dent;
	regex_t nodregex;

	if (use9p) {
		nnodes = 1;
		s = nodelist;
		while ((s = strchr(s, ',')) != NULL) {
			nnodes++;
			s++;
		}

		nodes = malloc(nnodes * sizeof(char *));
		s = strdup(nodelist);
		for(i = 0; i < nnodes; i++) {
			nodes[i] = s;
			s = strchr(s, ',');
			if (!s)
				break;

			*s = '\0';
			s++;
		}
	} else {
		dname = getenv("XCPUBASE");
		if (!dname)
			dname = "/mnt/xcpu";

		s = malloc(strlen(nodelist) + 3);
		sprintf(s, "^%s$", nodelist);
		if (regcomp(&nodregex, s, REG_EXTENDED | REG_NOSUB)) {
			fprintf(stderr, "invalid regular expression\n");
			exit(1);
		}

		dir = opendir(dname);
		if (!dir) {
			fprintf(stderr, "cannot open xcpu dir: %d\n", errno);
			exit(1);
		}

		nnodes = 0;
		while ((dent = readdir(dir)) != NULL) {
			if (*dent->d_name == '.')
				continue;

			nnodes++;
		}

		nodes = malloc(nnodes * sizeof(char *));
		rewinddir(dir);
		i = 0;
		while ((dent = readdir(dir)) != NULL) {
			if (*dent->d_name == '.')
				continue;

			if (regexec(&nodregex, dent->d_name, 0, NULL, 0) != REG_NOMATCH) {
				nodes[i] = malloc(strlen(dname) + strlen(dent->d_name) + 16);
				sprintf(nodes[i], "%s/%s/xcpu", dname, dent->d_name);
				i++;
			}
		}
		closedir(dir);
		nnodes = i;
		free(s);
	}
}

int
main(int argc, char **argv)
{
	int i, n, c, ecode;
	char **pargs, *s, *ename;
	Rxcmd rxc;

	while ((c = getopt(argc, argv, "9dn:e:l")) != -1) {
		switch (c) {
		case '9':
			use9p++;
			break;

		case 'd':
			npc_chatty++;
			break;

		case 'n':
			maxsessions = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'e':
			exec = strdup(optarg);
			break;

		case 'l':
			nocopy++;
			break;
		}
	}

	if (optind >= argc)
		usage();

	setup_env();
	setup_nodes(argv[optind++]);

	if (!nocopy) {
		if (optind >= argc)
			usage();

		copyfile = argv[optind];
		optind++;
	}

	if (!exec) {
		exec = strrchr(copyfile, '/');
		if (exec)
			exec++;
		else
			exec = copyfile;
	}

//	if (optind < argc) {
		pargs = malloc((argc-optind + 1) * sizeof(char*));
		if (!pargs) {
			fprintf(stderr, "not enough memory\n");
			exit(1);
		}

		pargs[0] = strdup(exec);
		n = strlen(exec) + 1;
		for(i = optind; i < argc; i++) {
			pargs[i - optind + 1] = quotestrdup(argv[i]);
			if (!pargs[i - optind + 1]) {
				fprintf(stderr, "not enough memory\n");
				exit(1);
			}

			n += strlen(pargs[i - optind + 1]) + 1;
		}

		args = malloc(n);
		if (!args)
			exit(1);

		*args = '\0';
		for(i = 0; i < argc-optind + 1; i++) {
			strcat(args, pargs[i]);
			strcat(args, " ");
			free(pargs[i]);
		}

		free(pargs);
		args[strlen(args) - 1] = '\0';
//	}

	rxc.nodesnum = nnodes;
	rxc.nodes = nodes;
	rxc.copypath = copyfile;
	rxc.exec = exec;
	rxc.argv = args;
	rxc.env = env;
	rxc.use9p = use9p;
	rxc.maxsessions = maxsessions;
	rxc.stdinfd = 0;
	rxc.stdoutfd = 1;
	rxc.stderrfd = 2;
	rxc.blksize = 8192;

	if (rxcmd(&rxc) < 0) {
		np_rerror(&ename, &ecode);
		fprintf(stderr, "error %d: %s\n", ecode, ename);
	}
	
	return 0;
}
