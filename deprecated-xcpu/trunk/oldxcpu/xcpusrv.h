/* note: this file is derived from /sys/src/cmd/ssh/ssh.h
  * Lucent Public License applies.
 */
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
#else
#include <String.h>
#endif

void	debug(int level, char *fmt, ...);

enum		/* internal debugging flags */
{
	DBG=			1<<0,
	DBG_FS=			1<<1,
	DBG_PACKET=		1<<2,
	DBG_CLIENT=		1<<3,
	DBG_PROC=		1<<4,
	DBG_PROTO=		1<<5,
	DBG_IO=			1<<6,
	DBG_SCP=		1<<7,
};


enum		/* protocol packet types */
{
	XCPU_MSG_STDOUT = 0,
	XCPU_MSG_STDERR, 
	XCPU_MSG_STDOUT_EOF,
	XCPU_MSG_STDERR_EOF,
	Xpu_Msg_Max,
};

enum		/* client types */
{
	Normal = 0,
	Persistent, /* do not remove the session when no clients present */
	TokenMaxArg = 3,	/* maximum number of arguments for tokenize */
};


typedef struct Msg Msg;
typedef struct Client Client;

struct Client
{
	QLock l;
	int ref;
	int state;
	int num;
	int type;	/* could be any of the types above */
/* to get around linux caching stupidity we will have to pack top bits of the rootgen name with 
  * a generation number. 
  */
	int gen;
	int servernum;
	char *connect;
	Req *rq;
	Req **erq;
	char *argv;
	
	/* support for chown */
	char *uid, *gid, *muid;
	int nuid, ngid;
	ulong mode;

	/* accumulates wait strings by children */
	String *waitstr;

	/* mq[0] is stdout, mq[1] is stderr */
	Msg *mq[2];
	Msg **emq[2];
	int mqeof[2];
	int lastpid;

	int stdinfd[2];
	int stdoutfd[2];
	int stderrfd[2];
	int stdoutpid, stderrpid;
	int waiterpid;
	/* our binary's tmp */
	char *tmpname;
	int tmpfd;
	int kidpid;
	int kiddead;
	int kideof;
	/* this is for tracking where we are */
	int haveargs, haveexec; 
};

struct Msg
{
	Client *c;
	uchar type;
	ulong len;		/* output: #bytes before pos, input: #bytes after pos */
	uchar *bp;	/* beginning of allocated space */
	uchar *rp;		/* read pointer */
	uchar *wp;	/* write pointer */
	uchar *ep;	/* end of allocated space */
	Msg *link;		/* for sshnet */
};

#define LONG(p)	(((p)[0]<<24)|((p)[1]<<16)|((p)[2]<<8)|((p)[3]))
#define PLONG(p, l) \
	(((p)[0]=(l)>>24),((p)[1]=(l)>>16),\
	 ((p)[2]=(l)>>8),((p)[3]=(l)))
#define SHORT(p) (((p)[0]<<8)|(p)[1])
#define PSHORT(p,l) \
	(((p)[0]=(l)>>8),((p)[1]=(l)))

extern Srv fs;
extern char Edecode[];
extern char Eencode[];
extern char Ememory[];
extern char Ehangup[];
extern int doabort;
extern int debuglevel;

/* msg.c */
void	msgdump(Msg *, char *);
Msg*	allocmsg(Client*, int, int);
void		badmsg(Msg*, int);
Msg*	recvmsg(Client*, int);
void		unrecvmsg(Client*, Msg*);
int		sendmsg(Msg*);
uchar	getbyte(Msg*);
ushort	getshort(Msg*);
ulong	getlong(Msg*);
char*	getstring(Msg*);
void*	getbytes(Msg*, int);
void		putbyte(Msg*, uchar);
void		putshort(Msg*, ushort);
void		putlong(Msg*, ulong);
void		putstring(Msg*, char*);
void		putbytes(Msg*, void*, long);
/* util.c */
void		debug(int, char*, ...);
void		error(char*, ...);
int		readstrnl(int, char*, int);

void 	procsread(Req *);
void 	procswrite(Req *, Client *);

/* os-specific stuff */
char *realuname;
char *getarch(void);
char *getuname(void);

#pragma varargck argpos error 1
#pragma varargck argpos sshlog 2


/* NOTE: The structure below remains to be used. unsure yet */

/* this structure is populated by xcpusrv from /proc
 * and then sent over the network in S-expressions
 * when a client reads "procs"
 *
 * this should be machine-independent but systems have their own ideas on
 * what they report, so no guarantees :(
 * 
 * linux changes /proc often, this will probably change often also
 */
typedef struct Prc Prc;
struct Prc
{
	/* from /proc/pid/stat */
	int pid;		/* process id */
	char *comm;		/* executable filename */
	char state;		/* R(unning), S(leeping), etc */
	int ppid;		/* parent pid */
	int pgrp;		/* process group */
	int session;	/* session id, usually only csh-set */
	int tty_nr;		/* tty used by proc */
	int tpgid;		/* "the process group id of the process which currently
					 * owns the tty that the process is connected to" */
	ulong flags;	/* math bit is decimal 4, traced bit is decimal 10 */
	ulong minflt;	/* number of minor faults (not requiring a memory page load from disk */
	ulong cminflt;	/* children's minflt */
	ulong majflt; 	/* major faults, loaded a page from disk */
	ulong cmajflt; 	/* children's majflt */
	ulong utime; 	/* jiffies scheduled in user mode */
	ulong stime; 	/* kernel mode jiffies */
	long cutime;	/* children's usermode jiffies */
	long cstime; 	/* children's kernel-mode jiffies */
	long priority;	/* standard nice value + 15 */
	long nice; 		/* -19 to 19 */
	long itrealvalue;	/* jiffies before the next SIGALRM is sent */
	ulong starttime; /* process started that many jiffies after system boot */
	ulong vsize;	/* virtual memory size */
	long rss;		/* resident set size (# pages process has in real memory - 3 */
	ulong rlim;		/* limit (bytes) on the rss of the process */
	ulong startcode;	/* address above which program text can run */
	ulong endcode;	/* address below which program text can run */
	ulong startstack;	/* stack start address */
	ulong kstkesp;	/* current esp value */
	ulong kstkeip;	/* current eip value */
	ulong signal;	/* bitmap of pending signals */
	ulong blocked; 	/* the bitmap of blocked signals */
	ulong sigignore;	/* bitmap of ignored signals */
	ulong sigcatch;	/* bitmap of caught signals ("catched" in the man page) */
	ulong wchan;	/* channel on which the process is waiting, usually a syscall */
	ulong nswap;	/* # pages swapped, unmaintained */
	ulong cnswap;	/* children nswap */
	int exit_signal;	/* parent will receive this when we die */
	int processor;	/* CPU we last executed on */

	/* from /proc/pid/statm; all in numbers of pages */
	ulong size;		/* total program size */
	ulong resident;
	ulong shared;
	ulong trs;		/* text (code) */
	ulong drs;		/* data/stack */
	ulong lrs;		/* library */
	ulong dt;		/* dirty pages */

	/* from /proc/pid/status */
	uint uid;
	uint euid;
	uint suid;
	uint fsuid;
	uint gid;
	uint egid;
	uint sgid;
	uint fsgid;
};


