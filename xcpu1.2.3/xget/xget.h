enum {
	/* root level files */
	Qroot = 1,
	Qctl,
	Qdata,
	Qavail,
	Qredir,
	Qlog,
	Qmax,
	Qchecksum,
	
	Bufsize = 4192,

	Dbgfn	= 1,
	Dbgclntfn = 1<<1,
	Dbgsrvfn = 1<<2,
	Dbgfs	= 1<<3,
	Dbgclnt	= 1<<4,
};

typedef struct File File;
typedef struct Server Server;
typedef struct Worker Worker;
typedef struct Usedworker Usedworker;
typedef struct Client Client;
typedef struct Req Req;

struct File {
	char*	nname;
	char*	lname;
	u64	datasize;
	u64	datalen;
	Spfile*	dir;
	Spfile* datafile;
	u8*     data;

	/* upstream data */
	Spcfsys*fs;
	Spcfid*	datafid;	/* used while reading the file */
	Spcfid*	availfid;
	Spcfd*	datafd;

	int	numworkers;
	Worker*	firstworker;
	Worker*	lastworker;
	Worker*	nextworker;
	Req*	reqs;
	File*	next;
	File*   prev;
	u32     checksum;
	u64     checksum_ptr;
	int     finished;
	time_t	progress;
	int     retries;
};

struct Server {
	char *saddress;
	int  conns;
	Server *next;
	Server *prev;
};

struct Worker {
	Worker*	prev;
	Worker*	next;
	Server* server;
	int     slevel;
};

struct Usedworker {
	Worker *worker;
	Usedworker *next;
	Usedworker *prev;
};

struct Client {
	char*   caddress;
	int*    clevels;
	Client* prev;
	Client* next;
	Usedworker* workersused;
};

struct Req {
	u64	offset;
	u32	count;
	Spreq*	req;
	Req*	next;
	Req*	prev;
};

/* xbootfs.c */
static void	usage(char *name);
static void	debug(int level, char *fmt, ...);
static void	fsinit(void);
static int	data_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int 	avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	avail_wstat(Spfile* file, Spstat* stat);
static void	avail_closefid(Spfilefid *fid);
static Spfile	*dir_next(Spfile *dir, Spfile *prevchild);
static Spfile	*dir_first(Spfile *dir);
static void     dir_destroy(Spfile *dir);
static int      checksum_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static Spfcall  *xflush(Spreq *req);
static int	log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	log_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static void	connopen(Spconn *conn);
static void	connclose(Spconn *conn);
static Spfile   *localfileread(Spfile *parent, char* filename);
static File     *filealloc(Spfile *parent, char *name, char *lname, 
			   u64 datasize, u64 datalen, u32 mtime, 
			   u32 mode, Spuser *user, Spgroup *group);
static int      file_finalize(File *f, int write);
static Spfile   *check_existance(Spfile *parent, char *name);
static int      matchsum(File *f);
