#include "xcpusrv.h"
#include <String.h>

typedef struct Procp Procp;
struct Procp {
	char *name;
	int *val;
	Procp *next;
};

enum {
	Pid = 0,
	Comm,		
	State,		
	Ppid,		
	Pgrp,		
	Session,	
	Tty_nr,		
	Tpgid,		
	Flags,	
	Minflt,	
	Cminflt,	
	Majflt, 	
	Cmajflt, 	
	Utime, 	
	Stime, 	
	cutime,	
	Cstime, 	
	Priority,	
	Nice, 		
	Itrealvalue,	
	Starttime, 
	Vsize,	
	Rss,		
	Rlim,		
	Startcode,	
	Endcode,	
	Startstack,	
	Kstkesp,	
	Kstkeip,	
	Signal,	
	Blocked, 	
	Sigignore,	
	Sigcatch,	
	Wchan,	
	Nswap,	
	Cnswap,	
	Exit_signal,	
	Processor,	

	/* from /proc/pid/statm */
	Size,		
	Resident,
	Shared,
	Trs,		
	Drs,		
	Lrs,		
	Dt,		

	/* from /proc/pid/status */
	Uid,
	Euid,
	Suid,
	Fsuid,
	Gid,
	Egid,
	Sgid,
	Fsgid,

	Proclast,				/* how many elements in total */

	Procstat 	= 37,		/* how many elements from stat */
	Procstatm	= 7,		/* how many elements from statm */
	Procstatus	= 8,		/* how many elements from status */
};

	
char 
*procnames[Proclast] = {
	"pid",
	"comm",
	"state",
	"ppid",
	"pgrp",
	"session",
	"tty_nr",
	"tpgid",
	"flags",
	"minflt",
	"cminflt",
	"majflt",
	"cmajflt",
	"utime",
	"stime",
	"cutime",
	"cstime",
	"priority",
	"nice",
	"itrealvalue",
	"starttime",
	"vsize",
	"rss",
	"rlim",
	"startcode",
	"endcode",
	"startstack",
	"kstkesp",
	"kstkeip",
	"signal",
	"blocked",
	"sigignore",
	"sigcatch",
	"wchan",
	"nswap",
	"cnswap",
	"exit_signal",
	"processor",
	"size",
	"resident",
	"shared",
	"trs",
	"drs",
	"lrs",
	"dt",
	"uid",
	"euid",
	"suid",
	"fsuid",
	"gid",
	"egid",
	"sgid",
	"fsgid",
};

void runit(void *ac) {
	int kidfds[3];
	Client *c = ac;
	/* kudos to p9p, they do a bidi pipe */
	int pid;
//	char err[128];
	char *argv[128];
	int ntoken;
	int i;

	ntoken = tokenize(c->argv, argv, 127);
	argv[ntoken] = 0;
//	debug(12, "ntokedn %d\n", ntoken);
//	for(i = 0; i < ntoken; i++)
//		debug(12, argv[i]);

	kidfds[0] = c->stdinfd[1];
	kidfds[1] = c->stdoutfd[1];
	kidfds[2] = c->stderrfd[1];

	pid = fork();
//	if (pid < 0)
//		debug(12, "FORK FAIL\n");
	if (pid > 0){
		(void)close(c->stdinfd[1]);
		(void)close(c->stdoutfd[1]);
		(void)close(c->stderrfd[1]);
		return;
	}

//	debug(12, "runit: spawn %s already\n", c->tmpname);

	dup(c->stdinfd[1], 0);
	dup(c->stdoutfd[1], 1);
	dup(c->stderrfd[1], 2);
	(void)close(c->stdinfd[0]);
	(void)close(c->stdoutfd[0]);
	(void)close(c->stderrfd[0]);

	exec(c->tmpname, argv);

	dup(2, open("/dev/cons", ORDWR));
//	debug(12, "Gosh Darn!! exec failed: %r\n");
}

void tmpexec(Client *c) {
		c->tmpname = strdup("/tmp/xcpuXXXXXXXXXXX");
		mktemp(c->tmpname);
		c->tmpfd = create(c->tmpname, ORDWR|ORCLOSE, 0700);
}

int mkdebugfd(void){
	char *console = "/dev/cons";
	int debugfd;
	debugfd = open(console, ORDWR);
	if (debugfd < 0)
		threadexits(smprint("NO %s\n", console));
	return debugfd;
}

static struct utsname sname = {
	.sysname = "",
};

char 
*getarch(void)
{
	if(sname.sysname[0] == '\0')
		uname(&sname);

	return sname.machine;
}

char 
*getuname(void) 
{
	if(sname.sysname[0] == '\0')
		uname(&sname);

	return sname.sysname;
}

/* contains a list of all requested values */
int procstr[Proclast];
/* contains pointers for the parsed tokens off /proc/pid/... */
char *proctok[Proclast];
int proclast = Proclast;

/* we accept a string (sexp?) of values we care for reporting */
void
procswrite(Req *r, Client *c)
{
	String *s;
	char *ptoks[proclast];
	char *chr;
	int i, j, toks, tmppstr[proclast];

	s = s_new();
	if(s == nil)
		respond(r, "can not allocate memory for data");

	/* BUG: we're assuming we fit a whole description in a single packet */
	s_nappend(s, r->ifcall.data, r->ifcall.count);

	/* flatten the sexp, it's not nested anyway, or shouldn't */
	if((chr = strchr(s_to_c(s), '(')) != nil)
		*chr = ' ';
	if((chr = strchr(s_to_c(s), ')')) != nil)
		*chr = ' ';

	
	toks = tokenize(s_to_c(s), ptoks, proclast);
	if(toks == 1 && strcmp(ptoks[0], "all") == 0) {
		for(i = 0; i < proclast; i++)
			tmppstr[i] = i;
	} else for(i = 0; i < toks; i++) {
		tmppstr[i] = -1;
		for(j = 0; j < proclast; j++) {
			if(strcmp(ptoks[i], procnames[j]) == 0)
				tmppstr[i] = j;
		}
		if(tmppstr[i] == -1) {
			s_free(s);
			s = s_new();
			s_append(s, "invalid string descriptor: ");
			s_append(s, ptoks[i]);
			s_append(s, "; transaction cancelled");
			respond(r, s_to_c(s));
			s_free(s);
			return;
		}
	}
	if(i < proclast)
		tmppstr[i] = -1;	/* terminate the list */

	for(i = 0; i < proclast; i++)
		procstr[i] = tmppstr[i]; 

	s_free(s);
	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
}