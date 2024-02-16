#include <u.h>
#include <libc.h>
#include <regexp.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <9p.h>

/* arghhhhhh!!!! */
#ifdef p9p
#include <libString.h>
#else
#include <String.h>
#endif

#include "mntclient.h"

Channel *input, *donechan;
char 	**dirno;
char	**fullpath;
char 	*base;
int 	nodeno, pflag;
int		*rdin;
int 	debuglevel, nodecount;
int 	totalenvsize = 32; /* leave some headroom */
int		interactive, localexec;
char	buf[Bufsize];

char *envvar, *argvar;
char *binary;


enum 
{
	Stack = 8192,
};

Reprog *hostreg;	/* the user-supplied host regexp is stored here */

void
usage(void)
{
	print("usage: %s [-lpi] [-D debuglevel] nodes binary [argv...]\n", argv0);
	threadexitsall("usage");
}

void
debug(int level, char *fmt, ...)
{
	va_list arg;

	if (debuglevel < level)
		return;
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
}

int
handler(void *v, char *note)
{
    USED(v);

    if(strncmp(note, "interrupted", 11) == 0)
        threadexitsall(note);
    
    return 0;
}

void
doinput(void *v)
{
	Ioproc *iop;
	char *buf;
	long i, len, fd = (long)v;
	int nc = nodecount;

	buf = emalloc9p(Bufsize);

	debug(7, "doinput: fd: %d\n", fd);

	iop = ioproc();
	if(iop == nil)
		sysfatal("doinput: ioproc: %r");

	/* synchronize with all kids opening channels */
	for(i = 0; i < nc; i++)
		recvul(input);

	debug(7, "doinput after: fd: %d\n", fd);
	while((len = ioread(iop, fd, buf, Bufsize)) > 0) {
		for(i = 0; i < nc; i++)  
			if(iowrite(iop, rdin[i], buf, len) != len)
				sysfatal("doinput: short write on stdin: %r");
	}

	closeioproc(iop);
	for(i = 0; i < nc; i++) {
		close(rdin[i]);
		writestring(fullpath[i], "ctl", "wipe\n");
	}


	free(buf);
	return;
}

void
openstdin(int nodeno, char *dirname, char *filename, int rfd)
{
	int r;
	char *name;

	name = smprint("%s/%s", dirname, filename);
	if(name == nil)
		sysfatal("openstdin: smprint: %r");

	debug(7, "openstdin: name: %s\n", name);
	r = open(name, OWRITE);
	if(r < 0)
		sysfatal("openstdin: can not open %s: %r", name);
	rdin[nodeno] = r;
	free(name);
	
	sendul(input, 0);
	debug(7, "sending donechan: %d\n", nodeno);
	sendul(donechan, nodeno);
	return;
}

void
dothread(void *v)
{
	Xcpuio xout, xerr;
	Channel *kids;
	long i = (long)v;
	int f, n;
	char *name, *buf;

	debug(7, "thread %d\n", i);

	if(i < 0 || i >= nodecount) {
		sendul(donechan, i);
		threadexits("bad node number");
	}

	buf = emalloc9p(Bufsize * sizeof(char));

	name = smprint("%s/clone", dirno[i]);
	if(name == nil)
		sysfatal("smprint failed in dothread: %r");
	debug(7, "thread %d: name: %s\n", i, name);

	f = open(name, OREAD);
	if(f < 0)
		sysfatal("can't open %s: %r", name);
	debug(7, "thread %d: fid: %d\n", i, f);

	n = read(f, buf, Bufsize);
	if(n <= 0)
		sysfatal("can not clone %s: %r", name);
	buf[n] = '\0';
	free(name);

	debug(7, "thread %d: buf: %s\n", i, buf);
	name = smprint("%s/%s", dirno[i], buf);
	fullpath[i] = name;

	debug(7, "thread %d: fullpath: %s\n", i, fullpath[i]);
	/* notify main thread that we're ready for input */
	if(! localexec)
		copybinary(dirno[i], buf, binary);

	/* echo the argv out */
	writestring(name, "argv", argvar);
	debug(2, "thread %d: wrote argv\n", i);

	/* exec it */
	if(writestring(name, "ctl", (localexec?"lexec\n":"exec\n")) < 0) {
		fprint(2, "can not start process at %s\n", name);
		if(interactive)
			sysfatal("unable to start a process");
		goto Done;
	}

	debug(2, "thread %d: wrote %s\n", i, localexec?"lexec":"exec");

	kids = chancreate(sizeof(ulong), 2);
	if(kids == nil)
		sysfatal("chancreate: kids: %r");

	xout.mnt = dirno[i];
	xout.session = buf;
	xout.name = "stdout";
	xout.fd = 1;
	xout.chan = kids;
	proccreate(catfile, (void *)&xout, Stack);

	xerr.mnt = dirno[i];
	xerr.session = buf;
	xerr.name = "stderr";
	xerr.fd = 2;
	xerr.chan = kids;
	proccreate(catfile, (void *)&xerr, Stack);

	if(interactive)
		openstdin(i, name, "stdin", 0);

	recvul(kids);
	recvul(kids);

Done:
	close(f);
	if(! interactive)
		sendul(donechan, i);

	debug(2, "thread %d: done\n", i);
	threadexits(nil);

}

void
threadmain(int argc, char *argv[])
{
	Dir *dir = 0;
	char *tmp;
	int totalargsize = 0;
	int f, i, d;

	base = getenv("xcpubase");
	if (base == nil)
		base = "/mnt/xcpu";

	ARGBEGIN{
	case 'D':	/* undocumented, debugging */
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'B':
		base = EARGF(usage());
		break;
	case 'p':
		pflag++;
		break;
	case 'l':
		localexec++;
		break;
	case 'i':
		interactive++;
		break;	
	default:
		usage();
	}ARGEND
	
	if(argc < 2) 
		usage();

	atnotify(handler, 0);

	tmp = smprint("^%s$", argv[0]);
	hostreg = regcomp(tmp);
	if(hostreg == nil)
		sysfatal("not a valid regular expression: %s: %r", tmp);
	free(tmp);

	/* done with regexp, the next are commands and arguments */
	argv++; argc--;


	f = open(base, OREAD);
	if(f < 0)
		sysfatal("can not open directory: %s: %r", base);

	d = dirreadall(f, &dir);
	close(f);
	if(d <= 0)
		sysfatal("error reading directory or directory empty: %s: %r", base);

	/* we actually allocate too many, but who cares? */
	dirno = malloc(d * sizeof(char *));
	if (dirno == nil)
		sysfatal("dirno malloc failed: %r");

	debug(7, "found %d directories\n", d);
	for(i = 0; i < d; i++) {
		debug(12, "matching dir against regexp: %s\n", dir[i].name);
		if(regexec(hostreg, dir[i].name, nil, 0)) {
			debug(12, "found matching dir: %s\n", dir[i].name);
			dirno[nodecount++] = smprint("/%s/%s/xcpu", base, dir[i].name);
		}
	}
	free(dir);

	debug(4, "found %d nodes\n", nodecount);
	if(nodecount == 0)
		sysfatal("no matching nodes found in %s", base);

	for(i = 0; i < argc; i++){
		totalargsize += strlen(argv[i]) + 2;
	}
	debug(4, "totalargsize: %d\n", totalargsize);
	argvar = mallocz(totalargsize, 1);
	if(! argvar) 
		sysfatal("argvar malloc failed: %r");

	binary = argv[0];
	for(i = 0; i < argc; i++){
		strcat(argvar, argv[i]);
		strcat(argvar," ");
	}
	debug(7, "argvar: %s\n", argvar);

	rdin = emalloc9p(nodecount * sizeof(int));
	fullpath = emalloc9p(nodecount * sizeof(char *));

	debug(7, "chancreate\n");
	input = chancreate(sizeof(ulong), nodecount);
	donechan = chancreate(sizeof(ulong), nodecount);
	if(input == nil || donechan == nil) 
		sysfatal("chancreate: %r");

	debug(7, "threadcreate: nodecount %d\n", nodecount);
	for(i = 0; i < nodecount; i++)
		proccreate(dothread, (void *)(long)i, Stack);

	/* wait here for notification from all nodes that they're about to exec */
	if (interactive)
		doinput((void *)0);

	for(i = 0; i < nodecount; i++) {
		debug(7, "recvul\n");
		recvul(donechan);
	}

	threadexitsall(nil);
}


