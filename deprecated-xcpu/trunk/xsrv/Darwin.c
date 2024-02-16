#include "xsrv.h"
#include <libString.h>

#include <sys/utsname.h>

static struct utsname 
sname = 
{
	.sysname = "",
};

char  *
getarch(void)
{
	if(sname.sysname[0] == '\0') {
		uname(&sname);
		if(strncmp(sname.machine, "Power", 5) == 0)
			strncpy(sname.machine, "powerpc", 8);
		realuname = smprint("/%s/%s", sname.sysname, sname.machine);
	}

	return sname.machine;
}

char *
getuname(void) 
{
	if(sname.sysname[0] == '\0') {
		uname(&sname);
		if(strncmp(sname.machine, "Power", 5) == 0)
			strncpy(sname.machine, "powerpc", 8);
		realuname = smprint("/%s/%s", sname.sysname, sname.machine);
	}
	return realuname;
}

/* from Linux.c, for now */
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

/* contains a list of all requested values */
int procstr[Proclast];
/* contains pointers for the parsed tokens off /proc/pid/... */
char *proctok[Proclast];
int proclast = Proclast;

void
procsinit(void)
{
	int i;

	/* initialize the array of strings reported by /procs */
	if(procstr[1] != 1) 
		for(i = 0; i < proclast; i++) 
			procstr[i] = i;
}

/* 
 * read all processes' stat files and pretty-print it to the client
 */
void
procsread(Req *r)
{
	Dir *dir = 0;
	Biobuf *statb, *statmb, *statusb;
	String *s;
	char *buf, name[64], *err = nil;
    int f, d, i, j;

	f = open("/proc", OREAD);
	if(f <= 0) {
		err = smprint("error opening /proc: %r");
		goto done;
	}
	
	d = dirreadall(f, &dir);
	close(f);
	if(d <= 0) {
		err = smprint("error reading /proc: %r");
		goto done;
	}

	s = s_new();
	if(s == nil) {
		err = "out of memory allocing Strings";
		goto done;
	}

	s_append(s, "(");
	for(i = 0; procstr[i] != -1 && i < Proclast; i++) {
		s_append(s, procnames[procstr[i]]);
		s_append(s, " ");

	}
	s_append(s, ")\n");

	for(i = 0; i < d; i++) {
		if('0' <= dir[i].name[0] && dir[i].name[0] <= '9') {
			s_append(s, "(");
			sprint(name, "/proc/%s/stat", dir[i].name);
			statb = Bopen(name, OREAD);
			if(statb == nil)
				goto wrapstat;	/* don't let this little failure stop our march */
			buf = Brdline(statb, '\n');
			if(tokenize(buf, proctok, Procstat) <= 0)
				goto wrapstat;

			sprint(name, "/proc/%s/statm", dir[i].name);
			statmb = Bopen(name, OREAD);
			if(statmb == nil)
				goto wrapstatm;
			buf = Brdline(statmb, '\n');
			if(tokenize(buf, proctok + Procstat, Procstatm) <= 0)
				goto wrapstatm;

			sprint(name, "/proc/%s/status", dir[i].name);
			statusb = Bopen(name, OREAD);
			if(statusb == nil)
				goto wrapstatus;	
			/* useless lines, but we can't skip ahead */
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			buf = Brdline(statusb, '\n');
			/* finally: uid! */
			buf = Brdline(statusb, '\n');
			if(tokenize(buf+5, proctok + Procstat + Procstatm, Procstatus/2) <= 0)
				goto wrapstatus;

			buf = Brdline(statusb, '\n');
			if(tokenize(buf+5, proctok + Procstat + Procstatm + Procstatus/2, Procstatus/2) <= 0)
				goto wrapstatus;
						

			for(j = 0; procstr[j] != -1 && j < Proclast; j++) {
				s_append(s, proctok[procstr[j]]);
				s_append(s, " ");

			}
wrapstatus:
			Bterm(statusb);
wrapstatm:
			Bterm(statmb);
wrapstat:
			Bterm(statb);
			s_append(s, ")\n");
		}
	}
	free(dir);

	readstr(r, s_to_c(s));

done:
	respond(r, err);
	if(err)
		free(err);
	return;
}


Sigstr sigstr[] = {
	"SIGHUP",	1,   /* hangup */
	"SIGINT",	2,   /* interrupt */
	"SIGQUIT",	3,   /* quit */
	"SIGILL",	4,   /* illegal instruction (not reset when caught) */
	"SIGTRAP",	5,   /* trace trap (not reset when caught) */
	"SIGABRT",	6,   /* abort() */
	"SIGPOLL",	7,   /* pollable event ([XSR] generated, not supported) */
	"SIGIOT",	6, 	/* compatibility */
	"SIGEMT",	7,   /* EMT instruction */
	"SIGFPE",	8,   /* floating point exception */
	"SIGKILL",	9,   /* kill (cannot be caught or ignored) */
	"SIGBUS",	10,  /* bus error */
	"SIGSEGV",	11,  /* segmentation violation */
	"SIGSYS",	12,  /* bad argument to system call */
	"SIGPIPE",	13,  /* write on a pipe with no one to read it */
	"SIGALRM",	14,  /* alarm clock */
	"SIGTERM",	15,  /* software termination signal from kill */
	"SIGURG",	16,  /* urgent condition on IO channel */
	"SIGSTOP",	17,  /* sendable stop signal not from tty */
	"SIGTSTP",	18,  /* stop signal from tty */
	"SIGCONT",	19,  /* continue a stopped process */
	"SIGCHLD",	20,  /* to parent on child stop or exit */
	"SIGTTIN",	21,  /* to readers pgrp upon background tty read */
	"SIGTTOU",	22,  /* like TTIN for output if (tp->t_local&LTOSTOP) */
	"SIGIO",	23,  /* input/output possible signal */
	"SIGXCPU",	24,  /* exceeded CPU time limit */
	"SIGXFSZ",	25,  /* exceeded file size limit */
	"SIGVTALRM",26,    /* virtual time alarm */
	"SIGPROF",	27,  /* profiling time alarm */
	"SIGWINCH",	28, /* window size changes */
	"SIGINFO",	29,  /* information request */
	"SIGUSR1",	30,  /* user defined signal 1 */
	"SIGUSR2",	31,  /* user defined signal 2 */
};

int
oskill(int pid)
{
	if(pid > -1)
		return kill(pid, 9);
	return -1;
}

int
ossig(int pid, char *s)
{
	int i;

	if(s == nil)
		return -1;

	/* only on lunix-like OS's we check whether the signal is a digit */
	if(s[0] >= '0' && s[0] <= '9')
		return kill(pid, atoi(s));

	for(i = 0; i < nelem(sigstr); i++)
		if(cistrncmp(s, sigstr[i].name, strlen(sigstr[i].name)) == 0)
			return kill(pid, sigstr[i].id);
	return -1;
}
