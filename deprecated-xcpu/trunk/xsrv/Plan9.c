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
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
			Brdline(statusb, '\n');
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

/* we accept a string (sexp?) of values we care for reporting */
void
procswrite(Req *r)
{
	String *s;
	char *ptoks[Proclast];
	char *chr;
	int i, j, toks, tmppstr[Proclast];

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

int 
checktemp(void) 
{
	char *tmp = "/tmp/xcpuXXXXXXXXXXX";
	int fd;

	debug(Dbgfn, "checktemp\n");

	tmp = mktemp(tmp);
	fd = create(tmp, ORDWD | OCEXEC, 0700);
	close(fd);
	remove(tmp);
	return fd;
}

int 
maketemp(Client *c) 
{
	c->tmpname = smprint("/tmp/xcpuXXXXXXXXXXX");
	c->tmpname = mktemp(c->tmpname);
	debug(Dbgfn, "maketemp: %s\n", c->tmpname);
	return create(c->tmpname, ORDWR | OCEXEC, 0700);
}

char *
getarch(void)
{

	return getenv("objtype");
}

char *
getuname(void) 
{
	if(realuname == nil)
		realuname = smprint("/%s/%s", "Plan9", getenv("objtype"));

	return realuname;
}

int
oschmod(char *file, ulong mode)
{
	Dir ndir;

	nulldir(&ndir);

	ndir.mode = mode;
	if(dirwstat(file, &ndir) == -1)
		return -1;

	return 0;
}

int
osspawn(int fd[], char *name, char **argv)
{
	int pid;

	debug(Dbgfn, "osspawn: %s\n", name);

	pid = fork();
	if (pid > 0){
		close(fd[0]);
		close(fd[1]);
		close(fd[2]);
		return pid;
	}

	dup(fd[0], 0);
	dup(fd[1], 1);
	dup(fd[2], 2);
 
	exec(name, argv);
	threadexitsall("exec");
	return -1;
}

void
dotufill(Srv *, Dir *, int, int, char *s)
{
	/* that's just silly */
	free(s);
}

void
dotuwfill(Dir *d)
{
	USED(d);
}

int
oskill(int pid)
{
	return postnote(PNPROC, pid, "kill");
}

int
ossig(int pid, char *s)
{
	return postnote(PNPROC, pid, s);
}

void
osspawner(void *v) 
{
	USED(v);
	return;
}

int
osfork(Client *c, int fd[], char *cmd, char **argv)
{
	int pid;
	switch(pid = fork()) {
	case -1:
		return -1;
	case 0:
		dup(fd[0], 0);
		dup(fd[1], 1);
		dup(fd[2], 2);
		exec(cmd, argv);
		threadexits("bad exec");
	default:
		close(fd[0]);
		close(fd[1]);
		close(fd[2]);
		c->pid = pid;
		return pid;
	}
}

void
osenvw(Req *r)
{
	char *str, *nstr, *tmp;
	int cnt;

	debug(Dbgfn, "osenvwrite\n");
	/* rethink this: what happens if an environment variable crosses
	 * boundaries?
	 */
	cnt = r->ifcall.count;
	str = r->ifcall.data;
	nstr = str;
	while(cnt > 0) {
		if(*nstr == '\n') {
			*nstr = '\0';
			tmp = strchr(str, '=');
			if(tmp != nil)
				*tmp++ = '\0';	
			putenv(str, tmp);
			str = nstr + 1;
		}
		cnt--;
		nstr++;
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
	return;
}

int
oschown(Client *c)
{
	Dir ndir;

	nulldir(&ndir);

	ndir.uid = c->uid;
	ndir.gid = c->gid;
	if(dirwstat(c->tmpname, &ndir) == -1)
		return -1;

	return 0;
}

void
osdaemon(void)
{
}

void
osenvr(void *v)
{
	Req *r = v;
	/* TODO: */
	readstr(r, "");
	respond(r, nil);
	threadexits(0);
}
