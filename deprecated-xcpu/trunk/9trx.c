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
String *sid[Maxnodes];
CFsys *fs[Maxnodes];
CFid *rdin[Maxnodes]; 	/* standard in, for -i */
char *nodes[Maxnodes];

int 	nodeno, pflag;
int		interactive, dokill, localexec;
char 	**dirno;
char 	*base;
int 	debuglevel, nodecount, group;

String *envvar, *argvar;
char *binary;

void
usage(void)
{
	print("usage: %s [-lpi] [-d] [tcp!]host1[!port],... binary [argv...]\n", argv0);
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
	for(i = 0; i < nc; i++)
		fsclose(rdin[i]);

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
	if (r == nil)
		sysfatal("openstdin: can not open %s: %r", name);
	rdin[nodeno] = r;
	free(name);
	sendul(note, 0);
	return;
}

char *
opensession(int n)
{
	char *addr = nodes[n];
	char *err = nil;
	if(strchr(addr, '!') == nil)
		addr = netmkaddr(addr, "tcp", "20001");
	fs[n] = xparse(addr); 
	if(fs[n] == nil) {
		err = smprint("fs: %r");
		goto Opendone;
	}

	
	sid[n] = fsclone(fs[n]);
	if(sid[n] == nil)
		err = smprint("sid: %r");

Opendone:
	return err;
}

void
runsingle(void *v)
{
	Xcpuio xout, xerr;
	Channel *kids;
	char *err = nil;
	int nodenum = (long)v;

	/* echo the argv out and exec it */
	debug(1, "runsingle: nodenum: %d, sess: %s\n", nodenum, s_to_c(sid[nodenum]));
	if(fswritestring(fs[nodenum], s_to_c(sid[nodenum]), "argv", s_to_c(argvar)) < 0)
		sysfatal("runsingle: argv: nodenum: %s, session: %s: %r", nodenum, s_to_c(sid[nodenum]));
	debug(1, "runsingle: exec: nodenum: %d, sess: %s\n", nodenum, s_to_c(sid[nodenum]));
	if(fswritestring(fs[nodenum], s_to_c(sid[nodenum]), "ctl", (localexec?"lexec\n":"exec\n")) < 0)
		sysfatal("runsingle: ctl: nodenum: %d, session: %s: %r", nodenum, s_to_c(sid[nodenum]));
	debug(1, "runsingle: done: %d...\n", nodenum);

	kids = chancreate(sizeof(ulong), 0);
	if(kids == nil) {
		err = smprint("kids chancreate: %r\n");
		goto Waitfs;
	}
	xout.mnt = "/";
	xout.session = s_to_c(sid[nodenum]);
	xout.name = "stdout";
	xout.fd = 1;
	xout.fs = fs[nodenum];
	xout.chan = kids;
	if(threadcreate((void *)fscatfile, &xout, Stack) < 0) {
		err = smprint("threadcreate xout: %r");
		goto Waitfs;
	}

	xerr.mnt = "/";
	xerr.session = s_to_c(sid[nodenum]);
	xerr.name = "stderr";
	xerr.fd = 2;
	xerr.fs = fs[nodenum];
	xerr.chan = kids;
	if(threadcreate((void *)fscatfile, &xerr, Stack) < 0) {
		err = smprint("threadcreate xerr: %r");
		goto Wait1;
	}

	if(interactive) {
		openstdin(fs[nodenum], nodenum, s_to_c(sid[nodenum]), "stdin", 0);
		sendul(input, 0);
	}

	recvul(kids);
Wait1:
	recvul(kids);
Waitfs:
	send(note, 0);
}

void
justrun(void *v)
{
	ulong node= (ulong)v;

	debug(1, "justrun: %d\n", node);
	if(! localexec)
		fscopybinary(fs[node], s_to_c(sid[node]), binary);

	proccreate(runsingle, (void *)node, Stack);
	threadexits(nil);
}

void
runspawn(void *v)
{
	String *name; 
	int nodenum = (long)v;
	long i, spawner;


	if(nodenum % group)
		spawner = nodenum - nodenum % group;
	else
		spawner = nodenum - group;

	debug(1, "runspawn: %d, tospawn: %d, session id: %s, binary: %s\n", nodenum, spawner, s_to_c(sid[spawner]), binary);

	fscopybinary(fs[spawner], s_to_c(sid[spawner]), binary);

	if(group > 1) {
		name = s_new();
		if(name == nil)
			sysfatal("s_new: %r");
		name = s_append(name, "spawn ");
	
		for(i = spawner + 1; i <= nodenum && i < nodecount; i++) {
			name = s_append(name, nodes[i]);
			name = s_append(name, ":");
			name = s_append(name, s_to_c(sid[i]));
			name = s_append(name, ",");
		}
		
		debug(1, "runspawn: %d: name: %s\n", nodenum, s_to_c(name));
		fswritestring(fs[spawner], s_to_c(sid[spawner]), "ctl", s_to_c(name));
		debug(1, "done runspawn: %d: name: %s\n", nodenum, s_to_c(name));
	}

	for(i = spawner; i <= nodenum && i < nodecount; i++) {
		debug(1, "runspawn: %d: proccreate: %d\n", nodenum, i);
		proccreate(runsingle, (void *)i, Stack);
	}
	threadexits(nil);
}

void
threadmain(int argc, char *argv[])
{
	char *addr, *err = nil;
	int i;

	ARGBEGIN{
	case 'd':
		debuglevel++;
		break;
	case 'l':
		localexec++;	/* no binary copy */
		break;
	case 'i':
		interactive++; 	/* interactive */
		break;
	case 'k':
		dokill++; 	/* interactive */
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
	group = (int)sqrt(nodecount);
	
	binary = argv[0];
	for(i = 0; i < argc; i++){
		argvar = s_append(argvar, argv[i]);
		argvar = s_append(argvar," ");
	}

	note = chancreate(sizeof(ulong), nodecount);
	if(interactive)
		input = chancreate(sizeof(ulong), nodecount);

	debug(1, "group: %d, nodes: %d\n", group, nodecount);

	if(localexec || group < 2) {
		for(i = 0; i < nodecount; i++) {
			if((err = opensession(i)) != nil)
				sysfatal(err);
			proccreate(justrun, (void *)(long)(i), Stack);
		}
	} else {
		for(i = 0; i < nodecount; i++) {
			if((err = opensession(i)) != nil)
				sysfatal(err);
			debug(2, "opened session for %d\n", i);
			if(((i+1) % group) == 0) {
				debug(2, "calling proccreate with node %d\n", i);
				proccreate(runspawn, (void *)(long)(i), Stack);
			}
		}
		if((i % group) == 1)
			proccreate(justrun, (void *)(long)(i-1), Stack);
		else if((i % group) != 0)
			proccreate(runspawn, (void *)(long)i, Stack); 
	}

	if(interactive)
		doinput(nil);

	for(i = 0; i < nodecount; i++)
		recvul(note);

	threadexitsall(0);
}
