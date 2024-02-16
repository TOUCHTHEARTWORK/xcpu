typedef struct Xsession Xsession;
typedef struct Xfilepipe Xfilepipe;
typedef struct Xfilebuf Xfilebuf;
typedef struct Xpipereq Xpipereq;
typedef struct Fsfid Fsfid;

enum {
	Read,
	Write,
};

struct Xfilepipe {
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	int		err;
	int		lfd;
	int		rfd;
	int		direction;
	pthread_t	thread;
	Nptrans*	trans;
	Xpipereq*	reqs;
	Xpipereq*	reqlast;
};

struct Xpipereq {
	int		cancelled;
	Xfilepipe*	pip;
	Npreq*		req;
	Npfcall*	rc;	/* for Tread */
	Xpipereq*	next;
};

struct Xfilebuf {
	int 		size;
	char*		buf;
};

struct Xsession {
	pthread_mutex_t	lock;
	int		refcount;
	int		id;
	int		state;
	int		mode;
	Xfilebuf	argv;
	Xfilebuf	env;
	Xfilebuf	ctl;

	Xfilepipe*	stin;
	Xfilepipe*	stout;
	Xfilepipe*	sterr;

	char*		dirpath;
	char*		execpath;
	int		execfd;
	int		pid;

	Npfile*		file;
	Npfile*		fsfile;
	Xsession*	next;
};

struct Fsfid {
	char*		path;
	int		omode;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
	struct stat	stat;
	Xsession*	xs;
};

Xfilepipe *pip_create(int direction);
void pip_destroy(Xfilepipe *p);
int pip_addreq(Xfilepipe* p, Npreq *req);
void pip_flushreq(Xfilepipe *p, Npreq *req);
void pip_close_remote(Xfilepipe *p);
void pip_close_local(Xfilepipe *p);

char *quotestrdup(char *str);
char *unquotestr(char *str, char **eptr);
char **tokenize(char *s);
int cutstr(unsigned char *target, int toffset, int tcount, char *src, int soffset);

int ufs_clone(Npfid *fid, Npfid *newfid);
int ufs_walk(Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall *ufs_open(Npfid *fid, u8 mode);
Npfcall *ufs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension);
Npfcall *ufs_read(Npfid *fid, u64 offset, u32 count, Npreq *);
Npfcall *ufs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *);
Npfcall *ufs_clunk(Npfid *fid);
Npfcall *ufs_remove(Npfid *fid);
Npfcall *ufs_stat(Npfid *fid);
Npfcall *ufs_wstat(Npfid *fid, Npstat *stat);
void ufs_fiddestroy(Npfid *fid);
void ufs_attach(Npfid *nfid, Xsession *xs, Npqid *qid);

int session_incref(Xsession *xs);
void session_decref(Xsession *xs);

#define QBITS		24
#define QMASK		((1<<QBITS) - 1)
#define QPATH(id)	((id + 1) << 24)
