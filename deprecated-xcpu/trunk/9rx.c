/* 
 * execute 9p commands directly over 9p
 */

#include <u.h>
#include <libc.h>
#include <regexp.h>
#include <bio.h>
#include <fcall.h>
#include <thread.h>
#include <9pclient.h>
#include <9p.h>

#ifdef p9p
#include <libString.h>
#else
#include <String.h>
#endif

#include "ninepclient.h"

enum 
{
	Stack = 8192*4,
	Maxnodes = 8192,
};

Reprog *hostreg;
Channel *note, *kidnote, *input;
CFid *rdin[Maxnodes]; 	/* stdin, for -i */
CFsys *sys[Maxnodes];
char *names[Maxnodes];
char *nodes[Maxnodes];

int 	nodeno, pflag;
int		interactive, localexec, dokill;
char 	**dirno;
char 	*base;
int 	debuglevel, nodecount;

String *envvar, *argvar;
char *binary;

void
usage(void)
{
	print("usage: %s [-lpi] [-D debuglevel] [tcp!]host1[!port],... binary [argv...]\n", argv0);
	threadexits("usage");
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

CFsys*
xparse(char *name)
{
	int fd;
	CFsys *fs;

	if((fd = dial(name, nil, nil, nil)) < 0)
		sysfatal("dial: %s: %r", name);
	if((fs = fsmount(fd, nil)) == nil)
		sysfatal("fsamount: %r");

	return fs;
}

void
doinput(void *v)
{
	Ioproc *iop;
	char *buf;
	long i, len, fd = (long)v;
	int nc = nodecount;

	buf = malloc(Bufsize);
	if (buf == nil)
		return;

	iop = ioproc();
	if(iop == nil)
		sysfatal("doinput: ioproc: %r");

	/* synchronize with all kids opening channels */
	for(i = 0; i < nc; i++)
		recvul(input);
	while((len = ioread(iop, fd, buf, Bufsize)) > 0) {
		for(i = 0; i < nc; i++)  
			if(fswrite(rdin[i], buf, len) != len)
				sysfatal("doinput: short write on stdin: %r");
	}

	closeioproc(iop);
	for(i = 0; i < nc; i++) {
		fsclose(rdin[i]);
		if(fswritestring(sys[i], names[i], "ctl", "wipe") < 0)
			sysfatal("%s: %s: ctl wipe: %r", sys[i], names[i]);
	}

	free(buf);
	return;
}

void
openstdin(CFsys *fs, int nodeno, char *dirname, char *filename, int rfd)
{
	CFid *r;
	char *name;

	name = smprint("%s/%s", dirname, filename);
	r = fsopen(fs, name, OWRITE);
	if(r == nil)
		sysfatal("openstdin: can not open %s: %r", name);
	rdin[nodeno] = r;
	free(name);
	sendul(input, 0);
	sendul(note, 0);
	return;
}

void
dothread(void *v)
{
	Xcpuio xout, xerr;
	CFsys *fs;
	Channel *kids;
	String *session;
	char *name, *addr = nodes[(long)v];
	char *err = nil;

	if(strchr(addr, '!') == nil)
		addr = netmkaddr(addr, "tcp", "20001");
	fs = xparse(addr); 
	if(fs == nil) {
		err = smprint("fs: %r");
		goto Done;
	}

	if(dokill) {
		fswritestring(fs, "/", "arch", "die");
		goto Done;
	}

	session = fsclone(fs);
	if(session == nil)
		sysfatal("can not open session to %s: %r", addr);

	sys[(long)v] = fs;
	name = smprint("/%s", s_to_c(session));
	if(! localexec) {
		fscopybinary(fs, name, binary);
	}
	names[(long)v] = name;

	/* echo the argv out and exec it */
	if(fswritestring(fs, name, "argv", s_to_c(argvar)) < 0)
		sysfatal("dothread: argv: session: %s: %r", name);

	if(fswritestring(fs, name, "ctl", (localexec?"lexec\n":"exec\n")) < 0)
		sysfatal("dothread: ctl: session: %s: %r", name);

	kids = chancreate(sizeof(ulong), 2);
	if(kids == nil) {
		err = smprint("kids chancreate: %r\n");
		goto Waitfs;
	}
	xerr.mnt = "/";
	xerr.session = name;
	xerr.name = "stderr";
	xerr.fd = 2;
	xerr.fs = fs;
	xerr.chan = kids;
	if(threadcreate((void *)fscatfile, &xerr, Stack) < 0) {
		err = smprint("threadcreate xerr: %r");
		goto Wait1;
	}
	xout.mnt = "/";
	xout.session = name;
	xout.name = "stdout";
	xout.fd = 1;
	xout.fs = fs;
	xout.chan = kids;
	if(threadcreate((void *)fscatfile, &xout, Stack) < 0) {
		err = smprint("threadcreate xout: %r");
		goto Waitfs;
	}


	if(interactive)
		openstdin(fs, (long)v, name, "stdin", 0);

	recvul(kids);
Wait1:
	recvul(kids);
Waitfs:
	s_free(session);
	free(name);
Done:
	if(! interactive)
		sendul(note, 0);
	threadexits(err);
}

void
threadmain(int argc, char *argv[])
{
	char *addr;
	int i;

	ARGBEGIN{
	case 'D':
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'l':
		localexec++;	/* no binary copy */
		break;
	case 'i':
		interactive++; 	/* interactive */
		break;
	case 'd':
		chatty9pclient++; 	/* talkative */
		break;
	case 'k':
		dokill++; 	/* die! */
		break;
	default:
		usage();
	}ARGEND
	
	if(argc < 2 && (!dokill)) 
		usage();

	atnotify(handler, 0);

	argvar = s_new();
	if(argvar == nil)
		sysfatal("s_new: %r\n");

	addr = argv[0];
	/* done with regexp, the next are commands and arguments */
	argv++; argc--;

	nodecount = gettokens(addr, nodes, Maxnodes, ",");
	
	binary = argv[0];
	for(i = 0; i < argc; i++){
		argvar = s_append(argvar, argv[i]);
		argvar = s_append(argvar," ");
	}

	input = chancreate(sizeof(ulong), nodecount);
	note = chancreate(sizeof(ulong), nodecount);
	if(input == nil || note == nil) 
		sysfatal("chancreate: %r");


	for(i = 0; i < nodecount; i++) {
		if(nodes[i] != nil)
			proccreate(dothread, (void *)(long)i, Stack);
	}

	if(interactive)
		doinput((void *)0);

	for(i = 0; i < nodecount; i++)
		recvul(note);

	threadexitsall(0);
}
