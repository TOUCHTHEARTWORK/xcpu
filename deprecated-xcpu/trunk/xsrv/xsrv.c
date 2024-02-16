#include "xsrv.h"

#define PATH(type, num, generation)    	((generation<<24)|(num<<8)|(type))
#define TYPE(path)						(((ulong)(path) & 0xFF))
#define NAMENUM(path)					((ulong)(path) & 0xFFFF)
#define NUM(path)						(((ulong)((path)>>8) & 0xFFFF))
#define QID(req)      					((ulong)(req->fid->qid.path))

/* needs to match the Tab structure below */
enum
{
	Qroot = 0,
	Qclone,
	Qprocs,
	Quname,
	Qarch,
	Qaddr,
	Qenv,
	Qn,
	Qexec,
	Qargv,
	Qctl,
	Qstdin,
	Qstdout,
	Qstderr,
	Qwait,
	Qio,
	Qcenv,
	Qlast,

};

Tab tab[] =
{
	"/",		Qroot, 0, 	DMDIR|0555, nil, nil, nil, 0, 0, 0,
	"clone",	Qclone, 0, 	0444, nil, nil, nil, 0, 0, 0,
	"procs",	Qprocs,	0, 	0664, nil, nil, nil, 0, 0, 0,
	"uname", 	Quname,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"arch", 	Qarch,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"addr", 	Qaddr,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"env", 		Qenv,	0, 	0664, nil, nil, nil, 0, 0, 0,
	nil,	Qn,		0, 	DMDIR|0755, nil, nil, nil, 0, 0, 0,
	"exec", 	Qexec,	0, 	0666, nil, nil, nil, 0, 0, 0,
	"argv", 	Qargv,	0, 	0666, nil, nil, nil, 0, 0, 0,
	"ctl",		Qctl,	0, 	0666, nil, nil, nil, 0, 0, 0,
	"stdin", 	Qstdin,	0, 	0666, nil, nil, nil, 0, 0, 0,
	"stdout", 	Qstdout,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"stderr", 	Qstderr,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"wait", 	Qwait,	0, 	0444, nil, nil, nil, 0, 0, 0,
	"io", 	Qio,	0, 	0666, nil, nil, nil, 0, 0, 0,
	"env", 	Qcenv,	0, 	0666, nil, nil, nil, 0, 0, 0,
};

/* from $OS.c */
extern char *procnames[];
extern char *proctok[];
extern int procstr[];
extern int proclast;


Client *clients;
QLock cl;

/* default userid and names for the server */
char *defuid = "root";
char *defgid = "root";
char *defmuid = "root";
uint defnuid = 0;  /* root */
uint defngid = 0;  /* root */
/* uid/gid, etc for clone and sessions */
char *uid, *gid, *muid;
int	nuid, ngid;

int nclient;
int debuglevel;
int	daemonize;
ulong time0;
ulong mtime, mode;
		
void
debug(int level, char *fmt, ...)
{
    va_list arg;
    if((level&debuglevel) == 0)
        return;
    va_start(arg, fmt);
    vfprint(2, fmt, arg);
    va_end(arg);
}

void
usage(void)
{
	fprint(2, "usage: %s [-D n] [-d] listen\n", argv0);
	threadexitsall("usage");
}

int
isvalid(int num)
{
	Client *c;

	if(num < 0 || num > nclient)
		return 0;

	c = &clients[num];
	if(c->inuse)
		return 1;
	
	return 0;
}

void
inproc(void *v)
{
	Client *c = v;
	Procio pio;
	char *err = nil, *thname;
	int n;
	
	debug(Dbgfn, "inproc: client: %p\n", c);

	thname = smprint("inproc[%uld]", c->num);
	threadsetname(thname);
	free(thname);

	while(recv(c->inchan, &pio) > 0) {
		debug(Dbgio, "%s: recv: Req: %p\n", threadgetname(), pio.r);
		if(pio.r == nil) {
			debug(Dbgthr, "%s: exiting (wipeclient)...\n", threadgetname());
			threadexits(nil);
		}

		c = pio.c;
		debug(Dbgio, "%s: writing %d bytes to fd: %d\n", threadgetname(), pio.r->ifcall.count, pio.fd);
		n = write(pio.fd, pio.r->ifcall.data, pio.r->ifcall.count);
		debug(Dbgio, "%s: wrote %d bytes\n", threadgetname(), n);
		switch(n) {
		case -1:
			err = "error writing";
			break;
		default:
			pio.r->ofcall.count = n;
			break;
		}
		respond(pio.r, err);
		err = nil;
	}
	debug(Dbgthr, "%s: exiting...\n", threadgetname());
	threadexits(nil);
}

void
errproc(void *v)
{
	Client *c = v;
	Req *r;
	Ioproc *iop;
	char *buf = nil, *thname;
	int n;

	debug(Dbgfn, "errproc: client: %p\n", c);

	thname = smprint("errproc[%uld]", c->num);
	threadsetname(thname);
	free(thname);

	iop = ioproc();
	if(iop == nil)
		threadexits("can not create ioproc for stderr");

	buf = malloc(Bufsize * sizeof(char));
	if(buf == nil)
		threadexits("no buffer for reading stderr: %r");

	while((n = ioread(iop, c->errfd[0], buf, Bufsize)) > 0) {
		char *tmp = buf;

		debug(Dbgio, "%s: read %d bytes %d\n", threadgetname(), n);
		while(n > 0) {
			r = (Req *)recvq(c->errq);
			debug(Dbgio, "%s: recv: req: %p\n", threadgetname(), r);
			if(r == nil) {
				debug(Dbgthr, "%s: exiting (wipeclient)...\n", threadgetname());
				qfree(c->errq);
				threadexits(nil);
			}
			if(n > r->ifcall.count) {
				memmove(r->ofcall.data, tmp, r->ifcall.count);
				r->ofcall.count = r->ifcall.count;
				tmp += r->ifcall.count;
				n -= r->ifcall.count;
			} else {
				memmove(r->ofcall.data, tmp, n);
				r->ofcall.count = n;
				n = 0;
			}
			respond(r, nil);
		}
	}
	debug(Dbgthr, "%s: exiting...\n", threadgetname());
	free(buf);
	closeioproc(iop);
	/* so we're dead pretty much, but there may still be requests coming in
	 * from clients, let them know that we've completed
	 */
	for(;;) {
		r = (Req *)recvq(c->errq);
		if(r == nil) {
			debug(Dbgthr, "%s: exiting (wipeclient)...\n", threadgetname());
			qfree(c->errq);
			threadexits(nil);
		}
		if(n < 0) {
			respond(r, "error reading stdout");
		} else {
			r->ofcall.count = 0;
			respond(r, nil);
		}
	}
}
void
outproc(void *v)
{
	Client *c = v;
	Req *r;
	Ioproc *iop;
	char *buf = nil, *thname;
	int n;

	debug(Dbgfn, "outproc: client: %p\n", c);

	thname = smprint("outproc[%uld]", c->num);
	threadsetname(thname);
	free(thname);
	iop = ioproc();
	if(iop == nil)
		threadexits("can not create ioproc for stdout");

	buf = malloc(Bufsize * sizeof(char));
	if(buf == nil)
		threadexits("no buffer for reading stdout: %r");

	while((n = ioread(iop, c->outfd[0], buf, Bufsize)) > 0) {
		char *tmp = buf;

		debug(Dbgio, "%s: read %d bytes %d\n", threadgetname(), n);
		while(n > 0) {
			r = (Req *)recvq(c->outq);
			debug(Dbgio, "%s: recv: req: %p\n", threadgetname(), r);
			if(r == nil) {
				debug(Dbgthr, "%s: exiting (wipeclient)...\n", threadgetname());
				qfree(c->outq);
				threadexits(nil);
			}
			if(n > r->ifcall.count) {
				memmove(r->ofcall.data, tmp, r->ifcall.count);
				r->ofcall.count = r->ifcall.count;
				tmp += r->ifcall.count;
				n -= r->ifcall.count;
			} else {
				memmove(r->ofcall.data, tmp, n);
				r->ofcall.count = n;
				n = 0;
			}
			respond(r, nil);
		}
	}
	debug(Dbgthr, "%s: exiting...\n", threadgetname());
	free(buf);
	closeioproc(iop);
	/* so we're dead pretty much, but there may still be requests coming in
	 * from clients, let them know that we've completed
	 */
	for(;;) {
		r = (Req *)recvq(c->outq);
		if(r == nil) {
			debug(Dbgthr, "%s: exiting (wipeclient)...\n", threadgetname());
			qfree(c->outq);
			threadexits(nil);
		}
		if(n < 0) {
			respond(r, "error reading stdout");
		} else {
			r->ofcall.count = 0;
			respond(r, nil);
		}
	}
}

int
initclient(Client *c, int num)
{
	int bad = 0;

	debug(Dbgfn, "initclient: client[%d]\n", num);

	/* need to make sure we're starting with a clean client, else
	 * the error checking at the end may fail
	 */
	qlock(&c->lock);
	c->inuse = 1;

	c->uid = c->gid = c->muid = nil;
	c->args = c->wstr = c->env = nil;
	c->errchan = c->outchan = c->inchan = nil;

	c->gen++;
	c->num = num;
	c->type = Normal;
	c->state = 0;
	c->ref = 0;
	c->pid = -1;
	c->uid = strdup(uid);
	if(c->uid == nil) {
		werrstr("strdup uid: %r");
		bad = 1;
		goto InitDone;
	}
	c->gid = strdup(gid);
	if(c->gid == nil) {
		werrstr("strdup gid: %r");
		bad = 1;
		goto InitDone;
	}
	c->muid = strdup(muid);
	if(c->muid == nil) {
		werrstr("strdup muid: %r");
		bad = 1;
		goto InitDone;
	}

	c->nuid = nuid;
	c->ngid = ngid;

	c->inpid = c->outpid = c->errpid = 0;
	c->outq = qalloc();
	if(c->outq == nil) {
		werrstr("qalloc outq: %r");
		bad = 1;
		goto InitDone;
	}
	c->errq = qalloc();
	if(c->errq == nil) {
		werrstr("qalloc errq: %r");
		bad = 1;
		goto InitDone;
	}
	c->waitq = qalloc();
	if(c->waitq == nil) {
		werrstr("qalloc outq: %r");
		bad = 1;
		goto InitDone;
	}

	c->perm = tab[Qclone].perm;
	c->infd[0] = c->infd[1] = -1;
	c->outfd[0] = c->outfd[1] = -1;
	c->errfd[0] = c->errfd[1] = -1;
	c->tmpfd = -1;
	c->ready = 0;

	c->inchan = chancreate(sizeof(Procio), 0);
	if(c->inchan == nil || (c->inpid = proccreate(inproc, c, Stack)) < 0) {
		werrstr("c->inchan: %r");
		bad = 1;
		goto InitDone;
	}

	c->args = s_new();
	c->wstr = s_new();
	c->env = s_new();
	if(c->args == nil || c->wstr == nil || c->env == nil) {
		werrstr("s_new: %r");
		bad = 1;
		goto InitDone;
	}
	snprint(c->name, 32, "%ulx", (c->gen<<16|c->num));

InitDone:
	qunlock(&c->lock);
	if(bad) {
		if(c->uid)
			free(c->uid);
		if(c->gid)
			free(c->gid);
		if(c->muid)
			free(c->muid);
		if(c->inchan) {
			send(c->inchan, nil); 
			chanfree(c->inchan);
		}

		if(c->args)
			s_free(c->args);
		if(c->wstr)
			s_free(c->wstr);
		if(c->env)
			s_free(c->env);

		if(c->inq)
			qfree(c->inq);
		if(c->outq)
			qfree(c->outq);
		if(c->errq)
			qfree(c->errq);
		if(c->waitq)
			qfree(c->waitq);
	}

	return (bad ? -1 : 0);
}

int
newclient(void)
{
	Client *tmpc;
	int i, ret;

	debug(Dbgfn, "newclient: total allocated clients: %d\n", nclient);
	qlock(&cl);
	for(i = 0; i < nclient; i++){
		debug(Dbgncl, "client[%d].inuse: %d\n", i, clients[i].inuse);
		if(clients[i].inuse == 0) {
			ret = initclient(clients+i, i);
			qunlock(&cl);
			return (ret == -1) ? ret : i;
		}
	}
	return -1;

	/* the code below needs to be rethought. we may run out of memory */

	if(nclient + Initclients >= 2<<16)
		return -1;
	tmpc = clients;
	clients = malloc((nclient + Initclients)*sizeof(Client));
	if(clients == nil) {
		clients = tmpc;
		return -1;
	}
	memset(clients, 0, (nclient + Initclients)*sizeof(Client));
	memcpy(clients, tmpc, nclient);
	nclient += Initclients;
	free(tmpc);
	
	debug(Dbgncl, "malloc: nclient = %d\n", nclient);
	qunlock(&cl);

	return newclient();
}

void
wipeclient(Client *c)
{
	debug(Dbgfn, "wipeclient: %p, num: %d\n", c, c->num);

	qlock(&c->lock);
	if(c->tmpname) {
		remove(c->tmpname);
		free(c->tmpname);
		c->tmpname = nil;
	}
	free(c->uid);
	free(c->gid);
	free(c->muid);

	close(c->infd[0]);
	close(c->outfd[0]);
	close(c->errfd[0]);
	close(c->tmpfd);

	send(c->inchan, nil);
	chanfree(c->inchan);
	if(c->outpid)
		sendq(c->outq, nil);
	if(c->errpid)
		sendq(c->errq, nil);

	qfree(c->waitq);

	c->inuse = 0;
	if(c->pid > -1)
		oskill(c->pid);
	c->pid = -1;

	s_free(c->args);
	c->args = nil;
	s_free(c->wstr);
	c->wstr = nil;
	s_free(c->env);
	c->env = nil;

	qunlock(&c->lock);
}

void
dorun(Req *r, Client *c, int local)
{
	String *arg;
	char *args[128];
	int nf, i, fd[3];

	if(local == 0 && c->tmpname == nil) {
		respond(r, "no binary");
		return;
	} 

	if (c->tmpfd >= 0) {
		respond(r, "Resource temporarily unavailable: no binary");
		return;
	}

	if(local == 0 && c->ready == 0) {
		respond(r, "Resource temporarily unavailable: client not ready");
		return;
	}

	if(! local)
		oschmod(c->tmpname, 0700);

	if(pipe(c->infd) < 0) {
		respond(r, "infd pipe");
		return;
	}
	if(pipe(c->outfd) < 0) {
		respond(r, "outfd pipe");
		return;
	}
	if(pipe(c->errfd) < 0) {
		respond(r, "errfd pipe");
		return;
	}

	fd[0] = c->infd[1];
	fd[1] = c->outfd[1];
	fd[2] = c->errfd[1];
	
	arg = s_copy(s_to_c(c->args));
	if(arg == nil) {
		respond(r, "Resource temporarily unavailable: s_copy in dorun: %r");
		return;
	}

	nf = tokenize(s_to_c(arg), args, 127);	/* fix: argmax? */
	if(nf == 0 && ! local) {
		args[0] = c->tmpname;
		args[1] = nil;
	} else
		args[nf] = nil;

	if(debuglevel & Dbgargv)
		for(i = 0; i < nf; i++)
			debug(Dbgargv, "args[%d]: %s\n", i, args[i]);

	c->outpid = proccreate(outproc, c, Stack);
	c->errpid = proccreate(errproc, c, Stack);
	if(c->outpid < 0 || c->errpid < 0) {
		free(arg);
		respond(r, "Resource temporarily unavailable: proccreate for std* failed: %r\n");
		return;
	}

	debug(Dbgexec, "dorun: running %s binary %s\n", 
			(local ? "local" : "copied"), 
			(local ? args[0] : c->tmpname));
	c->pid = osfork(c, fd, (local ? args[0] : c->tmpname), args);
	if(c->pid < 0) {
		close(c->infd[0]);
		close(c->infd[1]);
		close(c->outfd[0]);
		close(c->outfd[1]);
		close(c->errfd[0]);
		close(c->errfd[1]);
		s_free(arg);
		respond(r, "Resource temporarily unavailable: osfork failed");
		return;
	}
	s_free(arg);
	respond(r, nil);
	return;
}

void
spawner(void *v)
{
	Req *r = v;
	Client *c;
	Spawner *sp;
	Channel *note;
	char *f[Maxtokens], *s;
	char *nodes[Maxnodes];
	char *err;
	int i, nf, nodecount, group;
	ulong path = QID(r);

	c = &clients[NUM(path)]; 

	s = malloc((r->ifcall.count+1) * sizeof(char));
	if(s == nil) {
		respond(r, "out of memory in spawner");
		threadexits("out of memory");
	}

	memmove(s, r->ifcall.data, r->ifcall.count);
	s[r->ifcall.count] = '\0';

	debug(Dbgspawn, "[%d]: spawner: command: %s\n", c->num, s);

	/* expect: "spawn node1,node2..." */
	nf = tokenize(s, f, Maxtokens);
	if(nf != 2) {
		respond(r, "bad command");
		free(s);
		threadexits("command");
	}

	/* expect: tcp!node!port:session,... */
	nodecount = gettokens(f[1], nodes, Maxnodes, ",");
	group = (int)sqrt(nodecount);

	debug(Dbgspawn, "[%d]: spawner: nodecount: %d, group: %d\n", c->num, nodecount, group);

	sp = malloc(nodecount * sizeof(Spawner));
	if(sp == nil) {
		respond(r, "out of memory allocing sp");
		free(s);
		threadexits("sp malloc");
	}

	note = chancreate(sizeof(char), 0);
	if(note == nil) {
		respond(r, "out of channel memory in spawner");
		threadexits("out of memory");
	}

	if(group <= 0) {
		respond(r, "impossible spawn: not a single node");
	} else if(group == 1) {
		for(i = 0; i < nodecount; i++) {
			sp[i].path = nodes[i];
			sp[i].c = c;
			sp[i].chan = note;
			sp[i].spawn = nil;

			debug(Dbgspawn, "[%d]: spawner: just spawn: node: %d, path: %s\n", c->num, i, nodes[i]);
			if(proccreate(osspawner, sp+i, Stack) < 0) {
				err = smprint("error spawning to node %d out of %d: %r", i, nodecount);
				respond(r, err);
				free(err);
				free(sp);
				free(s);
				chanfree(note);
				threadexits("spawn");
			}
		}

		for(i = 0; i < nodecount; i++)
			recvul(note);
	} else {
		String *str;
		int j;

		for(i = 0; i < nodecount; i += group) {
			sp[i].spawn = nil;
			sp[i].path = nodes[i];
			sp[i].c = c;
			sp[i].chan = note;

			debug(Dbgspawn, "[%d]: spawner: recursive node: %d, path: %s\n", c->num, i, nodes[i]); 
			if(i < nodecount - 1) {
				/* there is at least one node we want to spawn to */
				str = s_new();
				if(str == nil) {
					err = smprint("out of String memory for recursive spawn to node %d, %r", i);
					respond(r, err);
					free(err);
					free(sp);
					free(s);
					chanfree(note);
					threadexits("spawn");
				}
				str = s_append(str, "spawn ");
				str = s_append(str, nodes[i+1]);
				for(j = i+2; j < nodecount && j < i+group; j++) {
					str = s_append(str, ",");
					str = s_append(str, nodes[j]);
				}
				sp[i].spawn = str;
			}

			if(proccreate(osspawner, sp+i, Stack) < 0) {
				err = smprint("error spawning to node %d out of %d: %r", i, nodecount);
				respond(r, err);
				free(err);
				free(sp);
				free(s);
				chanfree(note);
				threadexits("spawn");
			}
			recvul(note);
			if(sp[i].spawn != nil)
				s_free(sp[i].spawn);
		}
	}

	r->ofcall.count = r->ifcall.count;
	respond(r, nil);
	free(s);
	chanfree(note);
	threadexits(nil);
}

void
ctlwrite(Req *r, Client *c)
{
	char *f[Maxtokens], *s;
	char *err = nil;
	int nf;


	s = malloc((r->ifcall.count+1) * sizeof(char));
	if(s == 0) {
		err = "out of memory in ctlwrite";
		goto Badarg;
	}
	memmove(s, r->ifcall.data, r->ifcall.count);
	s[r->ifcall.count] = '\0';

	debug(Dbgfn, "ctlwrite: r=%p, c=%p, s=%s\n", r, c, s);

	nf = tokenize(s, f, Maxtokens);
	if(nf == 0) {
		err = "bad write";
		goto Badarg;
	}
	
	if(cistrncmp(f[0], "exec", 4) == 0) {
		r->ofcall.count = r->ifcall.count;
		free(s);
		dorun(r, c, 0);
		return;
	} else if(cistrncmp(f[0], "lexec", 5) == 0) {
		r->ofcall.count = r->ifcall.count;
		c->ready = 1;
		free(s);
		dorun(r, c, 1);
		return;
	} else if(cistrncmp(f[0], "spawn", 5) == 0) {
		if(nf != 2) {
			err = "no signal provided";	
			goto Badarg;
		}
		if(c->ready != 1) {
			err = "binary not ready";
			goto Badarg;
		}
		if(proccreate(spawner, r, Stack) < 0) {
			err = "can not start spawner process";
			goto Badarg;
		}
		free(s);
		return;
	} else if(cistrncmp(f[0], "import", 6) == 0) {
	} else if(cistrncmp(f[0], "wipe", 4) == 0) {
		wipeclient(c);
	} else if(cistrncmp(f[0], "signal", 6) == 0) {
		if(c->pid < 0) {
			err = "process not executing";
			goto Badarg;
		}
		if(nf != 2) {
			err = "no signal provided";	
			goto Badarg;
		}
		if(ossig(c->pid, f[1]) < 0) {
			err = "error issuing signal to process";
			goto Badarg;
		}
	} else if(cistrncmp(f[0], "type", 5) == 0) {
		if(nf != 2) {
			err = "no type for control message";
			goto Badarg;
		}

		/* set the state of the session:
		 * normal: close once all open files are closed
		 * persistent: keep the session
		 */
		if(cistrncmp(f[1], "normal", 6) == 0) {
			c->type = Normal;
		} else if(cistrncmp(f[1], "persistent", 10) == 0) {
			c->type = Persistent;
		} else {
			err = "bad type for control message";
			goto Badarg;
		}
		r->ofcall.count = r->ifcall.count;
	}

	r->ofcall.count = r->ifcall.count;
Badarg:
	respond(r, err);
	free(s);
}

void
xopen(Req *r)
{
	Client *c;
	char *err = nil;
	int n;
	ulong path = QID(r);

	debug(Dbgfn, "xopen: req: %p, path: %ux, num: %d, qid: %d, name: %s\n", 
			r, path, NUM(path), TYPE(path), tab[TYPE(path)].name);

	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	switch(TYPE(path)){
	case Qclone:
		n = newclient();
		if(n < 0) {
			err = "out of memory in newclient";
			break;
		}
		c = &clients[n];

		/* hack while waiting for v9fs fix, don't suid to nobody */
		if(strncmp(r->fid->uid, c->uid, strlen(r->fid->uid)) && strncmp(r->fid->uid, "nobody", 6))
			c->nuid = osgetuid(r->fid->uid);
		if(c->nuid < 0) {
			char *s = smprint("can not find uid for remote user %s", r->fid->uid);
			respond(r, s);
			free(s);
			return;
		}

		path = PATH(Qctl, c->num, c->gen);
		c->ref = 1;
		r->fid->qid.path = path;
		r->ofcall.qid.path = path;
		break;
	case Qio:
	case Qstdin:
	case Qstdout:
	case Qstderr:
		c = &clients[NUM(path)];
		debug(Dbgio, "xopen: client[%d].ref = %d\n", c->num, c->ref);
		if(c->ready == 0) {
			respond(r, "client not ready");
			return;
		} else {
			c->ref++;
			path = PATH(TYPE(path), c->num, c->gen);
			r->fid->qid.path = path;
			r->ofcall.qid.path = path;
		}
		break;
	default:
		if(TYPE(path) > Qn) {
			c = &clients[NUM(path)];
			c->ref++;
			debug(Dbgio, "xopen: client[%d].ref = %d\n", c->num, c->ref);
			path = PATH(TYPE(path), c->num, c->gen);
			r->fid->qid.path = path;
			r->ofcall.qid.path = path;
		}
		break;
	}
	respond(r, err);
}

void
xwrite(Req *r)
{
	Procio pio;
	Client *c;
	char *err = nil;
	ulong path = QID(r);

	debug(Dbgfn, "xwrite: req: %p, path: %ux, num: %d, qid: %d, name: %s\n", 
			r, path, NUM(path), TYPE(path), tab[TYPE(path)].name);

	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	switch(TYPE(path)){
	case Qcenv:
		c = &clients[NUM(path)];
		c->env = s_nappend(c->env, (char *)r->ifcall.data, r->ifcall.count);
		debug(Dbgargv, "xwrite: client env: %s\n", s_to_c(c->env));
		r->ofcall.count = r->ifcall.count;
		break;
	case Qenv:
		osenvw(r);
		return;
	case Qargv:
		c = &clients[NUM(path)];
		c->args = s_nappend(c->args, (char *)r->ifcall.data, r->ifcall.count);
		c->args = s_append(c->args, " ");
		debug(Dbgargv, "xwrite: client args: %s\n", s_to_c(c->args));
		r->ofcall.count = r->ifcall.count;
		break;
	case Qexec:
		c = &clients[NUM(path)];
		if(c->tmpname == nil) {
			c->tmpfd = maketemp(c);
			oschown(c);
		}
		if(c->tmpfd < 0 || c->tmpname == nil) {
			err = "can not create temp file";
		} else {
			pio.c = c;
			pio.fd = c->tmpfd;
			pio.r = r;
			debug(Dbgio, "xwrite: exec: fd: %d\n", pio.fd);
			send(c->inchan, &pio);
			return;
		}
		break;
	case Qio:
	case Qstdin:
		c = &clients[NUM(path)];
		if(c->infd[0] < 0) {
			err = "connection not established yet";
		} else {
			pio.c = c;
			pio.fd = c->infd[0];
			pio.r = r;
			debug(Dbgio, "xwrite: stdin: fd: %d\n", pio.fd);
			send(c->inchan, &pio);
			return;
		}
		break;
	case Qarch:
		if(r->ifcall.count >= 3 && cistrncmp(r->ifcall.data, "die", 3) == 0)
			threadexitsall("dead");
		else
			err = "no writes to arch";
		break;
	case Qprocs:
		procswrite(r);
		return;
	case Qctl:
		c = &clients[NUM(path)];
		ctlwrite(r, c);
		return;
	default:
		err = "not yet for this file";
		break;
	}
	respond(r, err);
}

void
fillstat(Dir *d, ulong path)
{
	Tab *t;
	Client *c;

	debug(Dbgfn, "fillstat: d %p, path 0x%llx, type %d, num %d \n", 
				  d, path, TYPE(path), NUM(path));

	memset(d, 0, sizeof(*d));

	d->qid.path = path;
	d->atime = d->mtime = time0;
	t = &tab[TYPE(path)];
	if(TYPE(path) != Qn && TYPE(path) != Qroot)
		d->name = strdup(t->name);

	d->qid.type = t->perm>>24;
	d->mode = t->perm;
	switch(TYPE(path)){
	case Qn:
		c = &clients[NUM(path)];
		if(d->name)
			free(d->name);
		d->name = strdup(c->name);
		d->uid = strdup(c->uid);
		d->gid = strdup(c->gid);
		d->muid = strdup(c->muid);
		dotufill(&fs, d, c->nuid, c->ngid, nil);
		break;
	case Qargv:
		c = &clients[NUM(path)];
		if (c->args) {
			debug(Dbgargv, "C%d:fillstat: c %p argv is %p \n",
			c->num, c, s_to_c(c->args));
			d->length = s_len(c->args);
		}
		d->uid = strdup(c->uid);
		d->gid = strdup(c->gid);
		d->muid = strdup(c->muid);
		dotufill(&fs, d, c->nuid, c->ngid, nil);
		break;
	case Qexec:
	case Qctl:
	case Qstdin:
	case Qstdout:
	case Qstderr:
	case Qwait:
	case Qio:
	case Qlast:
	case Qcenv:
		c = &clients[NUM(path)];
		d->uid = strdup(c->uid);
		d->gid = strdup(c->gid);
		d->muid = strdup(c->muid);
		dotufill(&fs, d, c->nuid, c->ngid, nil);
		break;
	case Qclone:
		d->uid = strdup(uid);
		d->gid = strdup(gid);
		d->muid = strdup(muid);
		dotufill(&fs, d, nuid, ngid, nil);
		d->mtime = mtime;
		d->mode = mode;
		break;
	case Qroot:
	case Qprocs:
	case Quname:
	case Qarch:
	case Qaddr:
	case Qenv:
		d->uid = strdup(defuid);
		d->gid = strdup(defgid);
		d->muid = strdup(defmuid);
		dotufill(&fs, d, defnuid, defngid, nil);
		break;
	}
}


int
rootgen(int i, Dir *d, void*v)
{
	int j;

	USED(v);

	debug(Dbgfn, "rootgen: i=%d\n", i);

	i += Qroot+1;

	if(i < Qn){
		fillstat(d, i);
		return 0;
	}

	i -= Qn;
	for(j = 0; j < nclient; j++) {
		if(clients[j].inuse && (i-- == 0)) {
			fillstat(d, PATH(Qn, clients[j].num, clients[j].gen));
			return 0;
		}
	}

	return -1;
}

int
clientgen(int i, Dir *d, void *aux)
{
    Client *c = aux;
	debug(Dbgfn, "clientgen: client[%d]: num: %d\n", c->num, i);

	/* 9p starts from 0, but we need to offset by Qn to get the first
	 * file in a client's directory
	 */
    i += Qn+1;

    if(i < Qlast){
		debug(Dbgio, "clientgen: client[%d]: %s\n", c->num, tab[i].name);
		fillstat(d, PATH(i, c->num, c->gen));
        return 0;
    }
    return -1;
}

void
xremove(Req *r)
{
	Client *c;
	char *err = nil;
	ulong path = QID(r);

	debug(Dbgfn, "xremove: req: %p path: %ux, num: %d, qid: %d, name: %s\n", 
			r, path, NUM(path), TYPE(path), tab[TYPE(path)].name);

	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	switch(TYPE(path)){
	case Qn:
		c = &clients[NUM(path)];
		wipeclient(c);
		break;
	default:
		err = "can not remove this file";
		break;
	}
	respond(r, err);
}
void
xread(Req *r)
{
	Client *c;
	char *err = nil;
	ulong path = QID(r);

	debug(Dbgfn, "xread: req: %p path: %ux, num: %d, qid: %d, name: %s\n", 
			r, path, NUM(path), TYPE(path), tab[TYPE(path)].name);

	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	switch(TYPE(path)){
	case Qprocs:
		procsread(r);
		return;
	case Qenv:
		proccreate(osenvr, r, Stack);
		return;
	case Qarch:
		readstr(r, getarch());
		break;
	case Quname:
		readstr(r, getuname());
		break;
	case Qaddr:
		err = "not implemented yet";
		break;
	case Qio:
	case Qstdout:
		c = &clients[NUM(path)];
		if(c->outfd[0] < 0) {
			err = "process not yet executing";
		} else {
			debug(Dbgio, "xread: stdout: fd: %d\n", c->outfd[0]);
			if(sendq(c->outq, r) < 0)
				respond(r, "error enqueueing read request for stderr");
			return;
		}
		break;
	case Qstderr:
		c = &clients[NUM(path)];
		if(c->errfd[0] < 0) {
			err = "process not yet executing";
		} else {
			debug(Dbgio, "xread: stderr: fd: %d\n", c->errfd[0]);
			if(sendq(c->errq, r) < 0)
				respond(r, "error enqueueing read request for stderr");
			return;
		}
		break;
	case Qexec:
		c = &clients[NUM(path)];
		if(c->tmpname != nil)
			readstr(r, c->tmpname);
		else
			err = "no binary copied";
		break;
	case Qargv:
		c = &clients[NUM(path)];
		readstr(r, s_to_c(c->args));
		break;
	case Qclone:
		err = "can't happen";
		break;
	case Qctl:
		c = &clients[NUM(path)];
		readstr(r, c->name);
		break;
	case Qroot:
		dirread9p(r, rootgen, nil);
		break;
    case Qn:
        dirread9p(r, clientgen, &clients[NUM(path)]);
        break;
	case Qwait:
		c = &clients[NUM(path)];
		if(sendq(c->waitq, r) < 0)
			respond(r, "error enqueueing read request for wait");
		break;
	case Qcenv:
		c = &clients[NUM(path)];
		readstr(r, s_to_c(c->env));
		break;
	default:
		err = "wrong permissions: you shouldn't be allowed to read from this file";
		break;
	}
	respond(r, err);
}

char*
xwalk1(Fid *fid, char *name, Qid *qid)
{
	Client *c;
	ulong i, n;
	ulong path = fid->qid.path;

	debug(Dbgfn, "xwalk1: newname: %s path: %ux, num: %d, qid: %d, name: %s\n", 
		name, path, NUM(path), TYPE(path), tab[TYPE(path)].name);

	if(!(fid->qid.type&QTDIR))
		return "walk in non-directory";

	if(strncmp(name, "..", 2) == 0){
		switch(TYPE(path)){
		case Qn:
			qid->path = PATH(Qroot, NUM(path), 0);
			qid->type = tab[Qroot].perm >> 24;
			return nil;
		case Qroot:
			return nil;
		default:
			return "bug in xwalk1";
		}
	}

	if((TYPE(path) == Qn) && !isvalid(NUM(path)))
		return "invalid client";

	/* only two-level directories means
	 * that we either walk from / or 
	 * from a directory corresponding to a session
	 * we expect to see only Qroot and Qn
	 */

	switch(TYPE(path)) {
	case Qroot:
		for(i = Qroot + 1; i < Qn; i++) {
			if(strncmp(name, tab[i].name, strlen(tab[i].name)) == 0){
				qid->path = PATH(tab[i].qid, 0, 0);
				qid->type = tab[i].perm >> 24;
				return nil;
			}
		}
		/* it's not a file, must be a session */
		n = strtoul(name, 0, 16);
		if(NAMENUM(n) > nclient)
			break;
		c = &clients[NAMENUM(n)];

		if (strncmp(name, c->name, strlen(c->name)) == 0){
			qid->path = PATH(tab[i].qid, c->num, c->gen);
			qid->type = tab[i].perm >> 24;
			return nil;
		}
		break;
	case Qn:
		c = &clients[NUM(path)];
		for(i = Qn + 1; i < nelem(tab); i++) {
			if(strncmp(name, tab[i].name, strlen(tab[i].name)) == 0){
				qid->path = PATH(tab[i].qid, c->num, c->gen);
				qid->type = tab[i].perm >> 24;
				return nil;
			}
		}
		break;
	default:
		sysfatal("walk in unknown directory: %s", tab[TYPE(path)].name);
	}

	return "no such file or directory";
}

void
xkillfid(Fid *f)
{
	Client *c;
	ulong path;

	path = (ulong)f->qid.path;

	debug(Dbgfn, "xkillfid: path: %ux, num: %d, qid: %d, name: %s, omode: %d\n", 
		path, NUM(path), TYPE(path), tab[TYPE(path)].name, f->omode);

	if(TYPE(path) > Qn) {
		c = &clients[NUM(path)];
		if(f->omode > -1) {
			c->ref--;

			switch(TYPE(path)){
			case Qexec:
				if(c->tmpfd >= 0) {
					debug(Dbgio, "xkillfid: tmpfd > 0 = %d\n", c->num, c->ref);
					close(c->tmpfd);
					c->tmpfd = -1;
					c->ready = 1;
				}
				break;
			}

			debug(Dbgio, "xkillfid: client[%d].ref = %d; type: %s\n", c->num, c->ref, (c->type == Normal ? "normal" : "persistent"));
			if(c->ref == 0) {
				if(c->type == Normal && (c->inuse == 1)) {
					/* sometimes a client is wiped through the "wipe" command */
					wipeclient(c);
				} else {
					c->ref = 1; /* there's always a ref for persistent clients */
				}
			}
		}
	}
}

void
xattach(Req *r)
{
	debug(Dbgfn, "xattach\n");

	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, "invalid attach specifier");
		return;
	}

	r->fid->qid.path = PATH(Qroot, 0, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

void
xstat(Req *r)
{
	ulong path = QID(r);
	debug(Dbgfn, "xstat\n");

	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

int
wfillstat(Dir *d, uvlong path)
{
	Client *c;

	debug(Dbgfn, "wfillstat: d %D, path 0x%llx, type %d, num %d\n", 
			d, path, TYPE(path), NUM(path));

	switch(TYPE(path)){
	case Qenv:
		break;
	case Qcenv:
		/* just support ftruncate case */
		if (d->length == 0) {
			c = &clients[NUM(path)];
			if (c->env) {
				s_free(c->env);
				c->env = s_new();
			}
		}
		break;
	case Qargv:
		/* just support ftruncate case */
		if (d->length == 0) {
			c = &clients[NUM(path)];
			if (c->args){
				s_free(c->args);
				c->args = s_new();
			}
		}
		break;
	case Qclone:
		if(d->uid[0] != '\0') {
			if(uid)
				free(uid);
			uid = strdup(d->uid);
		}
		if(d->gid[0] != '\0') {
			if(gid)
				free(gid);
			gid = strdup(d->gid);
		}
		if(d->muid[0] != '\0') {
			if(muid)
				free(muid);
			muid = strdup(d->muid);
		}

		dotuwfill(d);
		if(d->mode != (~0))
			mode = d->mode;

		/* do we need this? */
		break;
	case Qexec:
	case Qctl:
	case Qstdin:
		if(d->uid[0] != '\0' ||	d->gid[0] != '\0' ||
				d->muid[0] != '\0' || d->mode != (~0))
			return -1;
		break;
	default:
		return -1;
	}
	return 0;
}

static void
xwstat(Req *r)
{
	ulong path = QID(r);

	debug(Dbgfn, "xwstat\n");
	if((TYPE(path) >= Qn) && !isvalid(NUM(path))) {
		respond(r, "invalid client");
		return;
	}

	if(wfillstat(&r->d, r->fid->qid.path) < 0) {
		respond(r, "not allowed to wstat file");
		return;
	}
	
	respond(r, nil);
}

Srv fs = 
{
	.attach	=		xattach,
	.destroyfid	=	xkillfid,
	.walk1	=		xwalk1,
	.open	=		xopen,
	.read	=		xread,
	.write	=		xwrite,
	.stat	=		xstat,
//	.flush	=		xflush,
	.wstat	=		xwstat,
//	.end	=		xtakedown,
	.remove	=		xremove,
};

void
threadmain(int argc, char **argv)
{
	char *service;

	ARGBEGIN{
	case 'b':	/* daemonize, linux only */
		daemonize = 1;
		break;
	case 'u':	/* user id (string) */
		free(uid);
		uid = estrdup9p(EARGF(usage()));
		break;
	case 'g':	/* group id (string) */
		free(gid);
		gid = estrdup9p(EARGF(usage()));
		break;
	case 'U':	/* uid, numeric */
		nuid = strtol(EARGF(usage()), nil, 0);
		break;
	case 'G':	/* gid, numeric */
		ngid = strtol(EARGF(usage()), nil, 0);
		break;
	case 'd':
		chatty9p++;
		break;
	case 'D':
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();

	service = argv[0];

	if(checktemp() < 0)
		sysfatal("can not create files in /tmp");

    time0 = time(0);
    mtime = time0;
	/* some default user name file owners */
	if(uid == nil)
		uid = estrdup9p(defuid);
	if(gid == nil)
		gid = estrdup9p(defgid);
	if(muid == nil)
		muid = estrdup9p(defmuid);
    mode = 0440;    /* mode for Qclone */

	/* here we're allowed to fail */
	clients = emalloc9p(Initclients * sizeof(Client));
	nclient = Initclients;

	procsinit();

	if(daemonize)
		osdaemon();

	threadpostmountsrv(&fs, service, nil, MREPL|MCREATE);
	threadexits(nil);
}
