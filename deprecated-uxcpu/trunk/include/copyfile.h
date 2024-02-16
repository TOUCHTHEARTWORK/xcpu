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

typedef struct Cnpfile Cnpfile;

struct Cnpfile {
	Npcfsys*	fs;
	char*		name;

	/* internal use only */
	int		fd;
	Npcfid*		fid;
	char*		ename;
	int		ecode;
	u32		count;
	int		finished;
	pthread_mutex_t*lock;
	pthread_cond_t*	cond;
};

int copy_buf2npcfiles(char *buf, int buflen, int nfiles, Cnpfile *tofiles, u32 perm);
int copy_file2npcfiles(char *fname, int nfiles, Cnpfile *tofiles, int bsize);
int copy_file2files(char *fname, int ntargets, Cnpfile *tofiles, int blksize);
int copy_bufs2npcfiles(int nfiles, char **bufs, int *buflens, Cnpfile *tofiles, u32 perm);
