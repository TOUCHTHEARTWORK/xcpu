#include <u.h>
#include <libc.h>
#include <auth.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>
#include <bio.h>

/* arghhhhhh!!!! */
#ifdef p9p
#include <libString.h>
#include <9pclient.h>
#else
#include <String.h>
#endif

enum		/* client types */
{
	Normal = 0,
	Persistent, /* do not remove the session when no clients present */
	Remote = 0,	/* the client has a binary copied to it */
	Local = 1,	/* the client does not have a binary copied */

	/* various semi-constants */
	Stack = 32768,
	Bufsize = 8192,
	Initclients = 1024,
	Maxtokens = 3,	/* tokenize() */
	Maxnodes = 64, 	/* the maximum number of nodes we'll treespawn to */
};

enum		/* internal debugging flags */
{
	Dbgfn	=	1<<0,	/* function calls */
	Dbgio	=	1<<1,	/* io */
	Dbgthr	=	1<<2,	/* threads */
	Dbgncl	=	1<<3,	/* new client allocation */
	Dbgargv	=	1<<6,	/* arguments */
	Dbgexec	=	1<<7,	/* exec */
	Dbgspawn=	1<<8,	/* spawner */
};

typedef struct Client Client;
typedef struct Tab Tab;
typedef struct Procio Procio;
typedef struct Sigstr Sigstr;
typedef struct Spawner Spawner;
typedef struct Queue Queue;

struct Client
{
	QLock lock;
	char name[32];
	ulong ref;
	ulong state, num, gen, type;
	int pid;	/* pid of executing process */
	int inuse, ready;

	/* queue management for reasing/writing std* */
	Ioproc *iniop, *outiop, *erriop;
	Queue *inq, *outq, *errq, *waitq;
	int inpid, outpid, errpid;

	Channel *inchan;
	Channel *outchan;
	Channel *errchan;

	String *in;
	String *out;
	String *err;
	String *wstr;
	String *env;
	String *args;

	int infd[2], outfd[2], errfd[2];

	/* support for chown */
	char *uid, *gid, *muid;
	int nuid, ngid;
	ulong perm, time0;

	/* binaries copied to /tmp */
	char *tmpname;
	int tmpfd;
};

struct Tab
{
	char *name;
	ulong qid;
	ulong ref;
	ulong perm;
	char *uid, *gid, *muid;
	ulong nuid, ngid, nmuid;
};

struct Procio
{
	Client *c;
	Req *r;
	int fd;
};

struct Sigstr {
	char *name;
	int id;
};

struct Spawner {
	Client *c;
	Channel *chan;
	String *spawn;
	char *path;
};

typedef struct Qel Qel;
struct Qel
{
    Qel *next;
    void *p;
};

struct Queue
{
	Rendez r;
    QLock lk;
    Qel *head;
    Qel *tail;
};

extern Srv fs;
char *realuname;
int debuglevel;

void debug(int level, char *fmt, ...);

/* os-specific functions */
char 	*getarch(void);
char 	*getuname(void);
void 	procsread(Req *r);
void 	procswrite(Req *r);
void 	runit(Req *, Client *);
int 	maketemp(Client *);
int 	oschmod(char *, ulong);
int 	osfork(Client *c, int fd[], char *, char **);
int 	ossig(int, char *);
int 	oskill(int);
void 	dotufill(Srv *, Dir *, int, int, char *);
void 	dotuwfill(Dir *);
int 	oschown(Client *);
int 	osgetuid(char *);
void 	osenvr(void *);
void 	osenvw(Req *);
void 	oscenvw(String *);
void	osdaemon(void);
void	procsinit(void);
void	osspawner(void *v);

Queue 	*qalloc(void);
void 	qfree(Queue *);
void 	*recvq(Queue *);
int 	sendq(Queue *, void *);
int		checktemp(void);
