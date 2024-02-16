#include <sys/utsname.h>
#include <sys/types.h>
#include <signal.h>


#include "xsrv.h"

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
	String *s = nil;
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

	s_append(s, "(\n\t(");
	for(i = 0; procstr[i] != -1 && i < Proclast; i++) {
		s_append(s, procnames[procstr[i]]);
		s_append(s, " ");

	}
	s_append(s, ")\n");

	for(i = 0; i < d; i++) {
		if('0' <= dir[i].name[0] && dir[i].name[0] <= '9') {
			s_append(s, "\t(");
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
	s_append(s, ")\n");
	free(dir);

	readstr(r, s_to_c(s));

done:
	if(s)
		s_free(s);
	respond(r, err);
	if(err)
		free(err);
	return;
}


static struct utsname sname = {
	.sysname = "",
};

char *
getarch(void)
{
	if(sname.sysname[0] == '\0') {
		uname(&sname);
		realuname = smprint("/%s/%s", sname.sysname, sname.machine);
	}

	return sname.machine;
}

char *
getuname(void) 
{
	if(sname.sysname[0] == '\0') {
		uname(&sname);
		realuname = smprint("/%s/%s", sname.sysname, sname.machine);
	}
	return realuname;
}


Sigstr sigstr[] = {
	"SIGHUP",      1,   /* Hangup (POSIX).  */
	"SIGINT",      2,   /* Interrupt (ANSI).  */
	"SIGQUIT",     3,   /* Quit (POSIX).  */
	"SIGILL",      4,   /* Illegal instruction (ANSI).  */
	"SIGTRAP",     5,   /* Trace trap (POSIX).  */
	"SIGABRT",     6,   /* Abort (ANSI).  */
	"SIGIOT",      6,   /* IOT trap (4.2 BSD).  */
	"SIGBUS",      7,   /* BUS error (4.2 BSD).  */
	"SIGFPE",      8,   /* Floating-point exception (ANSI).  */
	"SIGKILL",     9,   /* Kill, unblockable (POSIX).  */
	"SIGUSR1",     10,  /* User-defined signal 1 (POSIX).  */
	"SIGSEGV",     11,  /* Segmentation violation (ANSI).  */
	"SIGUSR2",     12,  /* User-defined signal 2 (POSIX).  */
	"SIGPIPE",     13,  /* Broken pipe (POSIX).  */
	"SIGALRM",     14,  /* Alarm clock (POSIX).  */
	"SIGTERM",     15,  /* Termination (ANSI).  */
	"SIGSTKFLT",   16,  /* Stack fault.  */
	"SIGCLD",      17, /* Same as SIGCHLD (System V).  */
	"SIGCHLD",     17,  /* Child status has changed (POSIX).  */
	"SIGCONT",     18,  /* Continue (POSIX).  */
	"SIGSTOP",     19,  /* Stop, unblockable (POSIX).  */
	"SIGTSTP",     20,  /* Keyboard stop (POSIX).  */
	"SIGTTIN",     21,  /* Background read from tty (POSIX).  */
	"SIGTTOU",     22,  /* Background write to tty (POSIX).  */
	"SIGURG",      23,  /* Urgent condition on socket (4.2 BSD).  */
	"SIGXCPU",     24,  /* CPU limit exceeded (4.2 BSD).  */
	"SIGXFSZ",     25,  /* File size limit exceeded (4.2 BSD).  */
	"SIGVTALRM",   26,  /* Virtual alarm clock (4.2 BSD).  */
	"SIGPROF",     27,  /* Profiling alarm clock (4.2 BSD).  */
	"SIGWINCH",    28,  /* Window size change (4.3 BSD, Sun).  */
	"SIGPOLL",     29,   /* Pollable event occurred (System V).  */
	"SIGIO",       29,  /* I/O now possible (4.2 BSD).  */
	"SIGPWR",      30,  /* Power failure restart (System V).  */
	"SIGSYS",      31,  /* Bad system call.  */
	"SIGUNUSED",   31,
};

int
oskill(int pid)
{
	if(pid > -1)
		return kill(pid, 9);
	return 0;
}

int
ossig(int pid, char *s)
{
	int sig, i;

	if(s == nil)
		return -1;

	sig = atoi(s);
	/* only on lunix-like OS's we check whether the signal is a digit */
	if((s[0] >= '0') && (s[0] <= '9'))
		return kill(pid, atoi(s));

	for(i = 0; i < nelem(sigstr); i++)
		if(cistrncmp(s, sigstr[i].name, strlen(sigstr[i].name)) == 0)
			return kill(pid, sigstr[i].id);
	return -1;
}
