#include <u.h>
#include <libc.h>
#include <thread.h>
#include <regexp.h>
#include <bio.h>

/* arghhhhhh!!!! */
#ifdef p9p
#include <libString.h>
#endif

#ifndef p9p
#include <String.h>
#endif

Channel *req;
Channel *work;
Channel *done;

int 	nodeno;
char 	**dirno;
char 	*base;
int 	debuglevel, nodecount;

enum 
{
	Stack = 32768,
	Ntok = 50,	/* > number of fields in a report */
};

void
usage(void)
{
	print("usage: %s [-D debuglevel] [-m mtpt] [regex]\n", argv0);
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

void
readprocs(void *dirname)
{
	Biobuf *b = nil;
	String *s = nil, *ns = nil;
	char *name = nil, *c;
	char *err = nil, *tok[Ntok];
	int r, i;

	name = smprint("/%s/%s/xcpu/procs", base, (char *)dirname);
	b = Bopen(name, OREAD);
	if(b == nil) {
		err = "bad directory";	/* gracefully handle empty directories */
		goto Done;
	}

	s = s_new();
	if(s == nil) {
		err = "can not alloc strings";
		goto Done;
	}

	ns = s_new();
	if(ns == nil) {
		err = "can not alloc strings";
		goto Done;
	}

	while((c = s_read_line(b, s)) != nil) {
		r = tokenize(c, tok, Ntok);
		if(r <= 0)
			continue;
		if(s_len(ns) == 0)
			s_append(ns, "dir");
		else
			s_append(ns, dirname);
		for(i = 0; i < r; i++) {
			s_append(ns, "\t");
			s_append(ns, tok[i]);
		}
		s_append(ns, "\n");
	}

Done:
	send(req, 0);
	recv(work, &r);
	if(ns && (s_len(ns) > 0))
		print("%s", s_to_c(ns));
	send(done, 0);

	if(name)
		free(name);
	if(s)
		s_free(s);
	if(ns)
		s_free(ns);
	/* wrap up here so we don't waste time in the channels */
	if(b)
		Bterm(b);

	threadexits(err);
}

void
spawner(void *dn)
{
	char **c;

	for(c = (char **)dn; *c; c++) {
		debug(2, "spawner dir: %s\n", *c);
		proccreate(readprocs, (void *)*c, Stack);
	}	
	threadexits(nil);
}

void
threadmain(int argc, char *argv[])
{
	Dir *dir = 0;
	Reprog *hostreg;
	char **dirnames;
	char *tmp;
	int f, i, d, n = 0;

	base = getenv("XCPUBASE");
	if (base == nil)
		base = "/mnt/xcpu";

	ARGBEGIN{
	case 'D':	/* undocumented, debugging */
		debuglevel = strtol(EARGF(usage()), nil, 0);
		break;
	case 'B':
		base = EARGF(usage());
	}ARGEND
	
	if(argc > 1) 
		usage();

	req = chancreate(sizeof(int), 0);
	work = chancreate(sizeof(int), 0);
	done = chancreate(sizeof(int), 0);

	
    tmp = smprint("^%s$", (argc ? argv[0] : ".*"));
    hostreg = regcomp((char *)tmp);
    if(hostreg == nil)
        sysfatal("not a valid regular expression: %s: %r", tmp);

	f = open(base, OREAD);
	if(f < 0)
		sysfatal("can not open directory: %s: %r", base);

	d = dirreadall(f, &dir);
	if(d <= 0)
		sysfatal("error reading directory or directory empty: %s: %r", base);

	dirnames = mallocz((d + 1) * sizeof(char *), 0);
	if(dirnames == nil)
		sysfatal("cannot malloc dirnames: %r");

	for(i = 0; i < d; i++) {
		debug(2, "matching dir against regexp: %s\n", dir[i].name);
		if(regexec(hostreg, dir[i].name, nil, 0)) {
			debug(2, "found matching dir: %s\n", dir[i].name);
			dirnames[n++] = dir[i].name;
		}
	}
	dirnames[n] = nil;
	threadcreate(spawner, (void *)dirnames, Stack);

	do {
		recv(req, 0);	/* who's ready? */
		send(work, 0);	/* ok, you can work now */
		recv(done, 0);	/* done, thank you */
	} while(--n);

	close(f);
	free(dir);
	free(tmp);
	free(dirnames);
}
