#include <u.h>
#include <libc.h>
#include <thread.h>
#include <fcall.h>
#include <9p.h>
#include <bio.h>
#include <ctype.h>

/* arghhhhhh!!!! */
#ifdef p9p
#include <libString.h>
#else
#include <String.h>
#endif

#include <sys/mount.h>

/* BUGS: 
 * - in the real version perrors should be turned to syslogs
 */

enum {
	Qctl = 0,
	Qstatus,
	Qmax,

	Dbgfn	= 1,
	Dbgfs	= 1<<1,
	Dbgcfg	= 1<<2,
	Dbgthr	= 1<<3,
	Dbgloop	= 1<<3,
	Dbgrd	= 1<<4,
	Dbgwr	= 1<<5,
};

typedef struct Tab Tab;
typedef struct Node Node;

struct Tab
{
	char *name;
	ulong qid;
	ulong ref;
	ulong perm;
};

struct Node
{
	char *name;	/* what it's mounted as */
	char *dial;	/* its ip address */
	char *port;	/* used in mounting */
	char *host;
	char *ip;
	char *proto;
	int	 up;	/* set based on the following variables: */
	int	 nfd;	/* network connection */
	int	 mfd;	/* mount point */
	int	 ns;	/* number of active sessions */

	/* internal */
	int		err;	/* error in local functions */
	vlong	ts;
	QLock 	lk;
	Node 	*next;
};

Tab tab[] =	 
{   
	"ctl",		Qctl,	0,  0664,
	"status",	Qstatus,	0,  0444,
};

Tree	*root;
Node	*node, *n;
String	*s;		/* for replying */

char 	*base = "/mnt/xcpu";
char 	*defip = "127.0.0.1";
char 	*defport = "20001";
char 	*defproto = "tcp";
char	*service = "tcp!*!20002";
char 	*cfg = "xpfs.conf";
uint	debuglevel = 0xff;

void
usage(void)
{
	fprint(2, "usage: %s -[dD] [-c config] [-b base] [dial]\n", argv0);
	sysfatal("usage");
}

void
catcher(void *v, char *msg)
{
	if(strcmp(msg, "alarm") == 0)
		noted(NCONT);
	noted(NDFLT);
}

void
debug(int level, char *fmt, ...)
{
	va_list arg;

	if (!(debuglevel & level))
		return;
	va_start(arg, fmt);
	vfprint(2, fmt, arg);
	va_end(arg);
}
			
int
parsedial(char *addr, char **proto, char **host, char **ip, char **port)
{
	char *tmp, *tmp2;
	char *host2ip(char *host, char **ip);

	debug(Dbgfn, "parsedial: %s\n", addr);
	tmp = strchr(addr, '!');
	if(tmp == nil) {
		*host = addr;
		*proto = defproto;
		*port = defport;
		return 0;
	}

	tmp2 = tmp + 1;
	if(!strchr(tmp2, '!')) {
		werrstr("badly formed dial string: %s", addr);
		return -1;
	}

	*proto = addr;
	*tmp++ = '\0';	/* terminate proto */

	*host = tmp;
	tmp = strchr(*host, '!');
	*tmp++ = '\0';	/* terminate ip */

	*port = tmp;

	/* now fill in the IP. It is allowable to have the host name and IP be a dotted quad */
	/* if the host2ip fails, it gets filled in with 0.0.0.0, and the main loop will 
	  * exit if anything failed, but will try to read all the file. 
	  * Reason: let's let them see ALL the errors, not 
	  * one-at-a-time
	  */
	if (host2ip(*host, ip)) {
		debug(Dbgfn, "parsedial: proto: %s, host: %s, port: %s, NO IP\n", *proto, *host, *port);
		return -1;
	}

	debug(Dbgfn, "parsedial: proto: %s, host: %s, ip %s, port: %s\n", *proto, *host, *ip, *port);
	return 0;
}

void
freelist(Node *n)
{
	Node *tmp;

	while(n) {
		qlock(&n->lk);

		/* proto points to head of malloc'ed string, unless it's defproto, 
		  * in which case ip points to head of malloc'ed string. 
		  * this trick may be too clever for Ron.
		  */
		if(n->proto != defproto)
			free(n->proto);
		else
			free(n->host);

		free(n->ip);

		/* never free port -- it is contained in the smprint()'ed string */
		qunlock(&n->lk);

		tmp = n;
		n = n->next;
		free(tmp);
	}
}

/* confinit is allowed to sysfatal */
void
confinit(void)
{
	Biobuf *b;
	Node *nn;
	char *tmp;
	char *l, *c;
	int failed = 0;

	debug(Dbgfn, "confinit...\n");
	b = Bopen(cfg, OREAD);
	if(b == nil)
		sysfatal("bopen %s: %r", cfg);

	nn = node;
	while((l = Brdline(b, '\n')) != nil) {
		l[Blinelen(b) - 1] = '\0';
		l = strdup(l);
		if((c = strchr(l, '=')) != nil) {
			*c++ = '\0';
			debug(Dbgcfg, "config: %s=%s\n", l, c);
			if(node) {
				nn->next = emalloc9p(sizeof(Node));
				nn = nn->next;
			} else {
				nn = emalloc9p(sizeof(Node));
				node = nn;
			}
			nn->name = l;
			nn->dial = c;
			nn->nfd = -1;
			nn->mfd = -1;
			memset(&nn->lk, sizeof(QLock), 0);
			tmp = smprint("%s", nn->dial);
			if(tmp == nil)
				sysfatal("confinit: smprint: %r");
			if(parsedial(tmp, &nn->proto, &nn->host, &nn->ip, &nn->port) < 0){
				failed++;
				fprint(2, "bad ip address in dial string for node %s: %s\n", nn->name, nn->dial);
			}
			nn->err = 0;
		} else {
			debug(Dbgcfg, "config, illegal: %s\n", l);
		}
	}
	if (failed)
		sysfatal("%d errors in the config file (%s)\n", failed, cfg);
	Bterm(b);
}

int
ndial(char *addr)
{
	if(strchr(addr, '!') == nil)
		addr = netmkaddr(addr, "tcp", defport);

	return dial(addr, nil, nil, nil);
}

void
update(void *v)
{
	Node *n = v;
	Dir *dir = nil;
	Waitmsg *w;
	char *fn, *procs, *mntopt;
	char *args[8];
	int nfd = -1, mfd = -1, fd = -1, ns = 0;
	int i, j, d, pid;

	if(n == nil) {
		fprint(2, "update: nil node\n");
		threadexits("update: nil node");
	}
	debug(Dbgfn, "update: %s\n", n->name);
	debug(Dbgthr, "dial: %s, port: %s, host: %s, proto: %s\n", n->dial, n->port, n->ip, n->proto); 

	fn = smprint("%s/%s/xcpu", base, n->name);
	procs = smprint("%s/procs", fn);
	mntopt = smprint("port=%s", n->port);

	if(fn == nil || procs == nil || mntopt == nil) {
		qlock(&n->lk);
		n->err = 1;
		qunlock(&n->lk);
		fprint(2, "%s: smprint: %r\n", n->name);
		threadexits("local error: smprint");
	}

	/* see if we have a proper mount point, create if necessary */
	if(dirstat(fn) == nil)
		sysfatal("no such directory: %s: %r", fn);

	for(;;) {
		debug(Dbgloop, "%s: loop\n", n->name);
		nfd = n->nfd;
		mfd = n->mfd;
		ns = 0;

		if(nfd < 0)
			nfd = ndial(n->dial);
		if(nfd < 0)
			debug(Dbgloop, "%s: nfd: %d: %r\n", n->name, nfd);

		fd = open(procs, OREAD);
		if(fd < 0) {
			if(nfd >= 0) {
				mfd = 1;		/* assume the mount will happen, will be reset below if not */
				args[0] = "mount";
				args[1] = "-t";
				args[2] = "9P";
				args[3] = "-o";
				args[4] = mntopt;
				args[5] = n->ip;
				args[6] = fn;
				args[7] = nil;
			} else {
				/* no network connection, no mounting */
				mfd = -1;

				/* in fact, we'd better unmount */
				args[0] = "umount";
				args[1] = "-f";
				args[2] = fn;
				args[3] = nil;
			}

			for(i = 0; args[i] != nil; i++)
				debug(Dbgloop, "argv[%d] = %s; ", i, args[i]);
			debug(Dbgloop, "\n");

			switch(pid = fork()) {
			case -1: 
				qlock(&n->lk);
				n->err = 1;
				qunlock(&n->lk);
				fprint(2, "%s: fork error: %r\n", n->name);
				threadexits("fork");
			case 0:
				execvp(args[0], args);
				qlock(&n->lk);
				n->err = 1;
				qunlock(&n->lk);
				fprint(2, "%s: exec error: %r\n", n->name);
				threadexits("execvp");
			default:
				w = waitfor(pid);
				if(w->msg[0] != 0) {
					mfd = -1;
					fprint(2, "%s: mount returned error: %r\n", n->name);
				}
				break;
			}
			debug(Dbgloop, "%s: mfd: %d: %r\n", n->name, mfd);
		} else {
			if(nfd < 0)
				fprint(2, "martian result: %s mounted but inaccessible from the net\n", n->name);
			mfd = 1;
			close(fd);
		}

		if(mfd >= 0) {
			if(dir) {
				free(dir);
				dir = nil;
			}
			fd = open(fn, OREAD);
			if(fd >= 0) {
				d = dirreadall(fd, &dir);
				close(fd);
				for(i = 0; i < d; i++) {
					int yes = 0;
					for(j = 0; j < strlen(dir[i].name); j++) {
						if(! isxdigit(dir[i].name[j])) {
							yes = 0;
							goto Not;
						} else
							yes = 1;
					}
Not:
					ns += (yes ? 1 : 0);
				}
				debug(Dbgloop, "%s: ns: %d\n", n->name, ns);
			} else {
				debug(Dbgloop, "%s: can not open %s: %r\n", n->name, fn);
			}
		}
		/* here's where the actual update happens, note
		 * that we shouldn't do anything requiring a timeout
		 * when holding a lock
		 */
		qlock(&n->lk);
		n->nfd = nfd;
		n->mfd = mfd;
		n->ns = ns;
		n->ts = time(0);
		if(n->nfd < 0 || n->mfd < 0)
			n->up = 0;
		else
			n->up = 1;
		qunlock(&n->lk);
		sleep(5000);
	}
}

void
fsread(Req *r)
{
	Node *n;
	char *str;
	long path = (long) r->fid->file->aux;

	debug(Dbgfn, "fsread: req: %p name: %s, size: %d, off: %d\n", 
			r, tab[path].name, r->ifcall.count, r->ifcall.offset);

	switch(path) {
	case Qstatus:
		s_append(s, "name:\tstatus:\tsess:\n");
		n = node;
		while(n != nil) {
			qlock(&n->lk);
			if(n->err) {
				debug(Dbgrd, "%s: %s\n", n->name, "err");
				str = smprint("%s\t%s\n", n->name, "err");
			} else {
				debug(Dbgrd, "%s: %s\t%d\n", n->name, (n->up ? "up" : "down"), n->ns);
				str = smprint("%s\t%s\t%d\n", n->name, (n->up ? "up" : "down"), n->ns);
			}
			qunlock(&n->lk);
			if(str) {
				s_append(s, str);
				free(str);
				str = nil;
			}
			n = n->next;
		}
		readstr(r, s_to_c(s));
		s_reset(s);
		break;
	case Qctl:
		break;
	}
	respond(r, nil);
}


void
fswrite(Req *r)
{
	debug(Dbgfn, "fswrite: %p\n", r);
	respond(r, "not implemented yet");
}

Srv fs=
{
	.read	=	fsread,
	.write	=	fswrite,
};

void
threadmain(int argc, char **argv)
{
	File *f;
	int i, fd[2];

	ARGBEGIN{
	case 'd':
		chatty9p++;
		break;
	case 'D':
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'b':
		base = EARGF(usage());
		break;
	case 'c':
		cfg = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND
	if(argc == 1)
		service = argv[0];
	else if(argc > 1)
		usage();

	s = s_new();
	if(s == nil)
		sysfatal("s_new: %r");

	root = fs.tree = alloctree("root", "root", DMDIR|0555, nil);
	if(root == nil)
		sysfatal("creating tree: %r");
	debug(Dbgfs, "create root: %p\n", root);
	for(i = Qctl; i < Qmax; i++) {
		f = createfile(root->root, tab[i].name, "root", tab[i].perm, (void *)tab[i].qid);
		if(f == nil)
			sysfatal("creating %s: %r", tab[i].name);
		debug(Dbgfs, "create %s: %p\n", tab[i].name, f);
	}

	confinit();
	n = node;
	while(n != nil) {
		proccreate(update, n, 32*1024);
		n = n->next;
	}

	if(!fs.nopipe){
		if(pipe(fd) < 0)
			sysfatal("pipe: %r");
		fs.infd = fs.outfd = fd[1];
		fs.srvfd = fd[0];
	}
	if(post9pservice(fs.srvfd, service) < 0)
		sysfatal("post9pservice %s: %r", service);
	srv(&fs);
}

