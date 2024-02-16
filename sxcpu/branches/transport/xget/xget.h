#ifndef XCPU_H
#define XCPU_H
 
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
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
#include <regex.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/mman.h>

#include "spfs.h"
#include "spclient.h"
#include "strutil.h"
#include "xcpu.h"

enum {
	/* root level files */
	Qroot = 1,
	Qctl,
	Qdata,
	Qavail,
	Qredir,
	Qlog,
	Qmax,
	
	Bufsize = 4192,

	Dbgfn	= 1,
	Dbgfs	= 1<<1,
	Dbgclnt	= 1<<2,
};

typedef struct Req Req;

struct Req {
	u64	offset;
	u32	count;
	Spreq*	req;
	Req*	next;
	Req*	prev;
};

typedef struct Worker Worker;

struct Worker {
	char*	ip;
	int	port;
	/* the worker is alive until this time. Time is in seconds. */
	time_t until;
	Worker*	prev;
	Worker*	next;
};

typedef struct File File;

struct File {
	char*	name;
	char*	data;		/* pointer to the mmaped file */
	int	datasize;
	int	datalen;
	Spfile*	dir;

	/* upstream data */
	Spcfid*	datafid;	/* used while reading the file */
	Spcfid*	availfid;

	int	numworkers;
	Worker*	firstworker;
	Worker*	lastworker;
	Worker*	nextworker;
	Req*	reqs;
	File*	next;
	File*   prev;
};

File *files;
Spuser *user;
Spfile *root;
Spcfid *logfid;
Spsrv *srv;
unsigned int debuglevel;
Spuserpool *upool;
Spgroup *group;
int numconnects;
char* xgetpath;
int recursive;
char *namebuf;

/* common.c */
static void	usage(char *name);
void     	debug(int level, char *fmt, ...);
void	        connopen(Spconn *conn);
void	        connclose(Spconn *conn);
Spfcall         *xflush(Spreq *req);
int             init_server(int port);
int             init_xget_usr();

/* xgetfs.c */
void	        fsinit(void);
Spfile	        *dir_next(Spfile *dir, Spfile *prevchild);
Spfile	        *dir_first(Spfile *dir);

/* Server only */
int             dir_update(Spfile *dir);
void            dir_remove(Spfile *dir);
int             fullfilename(Spfile *file, char** fullname);
int             localfileread(Spfile *parent, char *filename);

/*
static int	data_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int 	avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	avail_wstat(Spfile* file, Spstat* stat);
static void	avail_closefid(Spfilefid *fid);
static int	log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static void	connopen(Spconn *conn);
static void	connclose(Spconn *conn);
*/

#endif
