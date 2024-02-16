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

typedef struct Rxfile Rxfile;
typedef struct Rxsession Rxsession;
typedef struct Rxpipe Rxpipe;
typedef struct Rxcmd Rxcmd;

struct Rxfile {
	Npcfid*		fid;
	int		fd;
	u64		off;
	int		(*read)(Rxfile *, char *, int);
	int		(*write)(Rxfile *, char *, int);
};

struct Rxpipe {
	pthread_t	thread;
	Rxsession*	rx;
	Rxfile*		file;
	int		fd;
	char*		ename;
	int		ecode;
};

struct Rxsession {
	int		finished;
	char*		arch;
	char*		mntpt;
	char*		addr;
	char*		sid;
	Npcfsys*	fsys;
	Rxfile*		wait;
	Rxfile*		ctl;
	Rxfile*		stin;
	Rxfile*		stout;
	Rxfile*		sterr;
	Rxpipe*		outpipe;
	Rxpipe*		errpipe;

	pthread_t	waitproc;
	char*		exitcode;
	pthread_mutex_t*lock;
	pthread_cond_t*	cond;
};

struct Rxcmd {
	int	nodesnum;
	char**	nodes;
	char*	copypath;
	char*	exec;
	char*	argv;
	char*	env;
	int	use9p;
	int	maxsessions;
	int	stdinfd;
	int	stdoutfd;
	int	stderrfd;
	int	blksize;
};

Rxfile *rxfile_open(char *path, int mode);
Rxfile *rxfile_9open(Npcfsys *fsys, char *path, int mode);
void rxfile_close(Rxfile *rxf);
int rxfile_read(Rxfile *f, char *buf, int buflen);
int rxfile_write(Rxfile *f, char *buf, int buflen);


Rxsession *rxsession_create(char *mntpt, int stdoutfd, int stderrfd, 
	int stdinfd, pthread_mutex_t *lock, pthread_cond_t *cond);
Rxsession *rxsession_9create(char *addr, int stdoutfd, int stderrfd, 
	int stdinfd, pthread_mutex_t *lock, pthread_cond_t *cond);
int rxsession_wipe(Rxsession *rx);
void rxsession_destroy(Rxsession *rx);

int rxcmd(Rxcmd *r);
char *rxmntpt2addr(char *mntpt);
