#include "xcpusrv.h"
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


