#include <u.h>
#include <libc.h>
#include <thread.h>
#include <regexp.h>

void shell(void);

int 	nodeno;
char 	**dirno;
char 	*base;
int 	debuglevel, nodecount;
int 	totalenvsize = 32; /* leave some headroom */

char *envvar, *argvar;
char *binary;


enum 
{
	Stack = 32768,
};

Reprog *hostreg;	/* the user-supplied host regexp is stored here */

void
usage(void)
{
	print("usage: %s [-D debuglevel] [-m mtpt] [user@]hostname\n", argv0);
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
copy(int from, int to) {
	int r, w;
	char buf[8192];
	while ((r = read(from, buf, sizeof(buf))) > 0) {
		w = write(to, buf, r);
		if (w != r)
			return -1;
	}
	return 0;
}

void
copybinary(void *dirname){
	char *name;
	int rfd, wfd;

	rfd = open(binary, OREAD);
	if (rfd < 0) {
		perror(binary);
		threadexitsall(binary);
	}

	name = smprint("%s/exec", (char *)dirname);
	wfd = open(name, OWRITE);
	if (wfd < 0) {
		perror(name);
		threadexitsall(name);
	}
	if(copy(rfd, wfd) < 0)
		sysfatal("copybinary: could not copy binary to %s", name);

	close(wfd);
	free(name);

	threadexits(0);
}

void
catfile(char *dirname, char *filename, int wfd){
	char *name;
	int rfd;

	name = smprint("%s/%s", dirname, filename);

	rfd = open(name, OREAD);
	if (rfd < 0) {
		perror(name);
		threadexitsall(name);
	}
	debug(2, "catfile: copy %s(%d) to %d\n", name, rfd, wfd);
	free(name);
	copy(rfd, wfd);
	if(copy(rfd, wfd) < 0)
		sysfatal("catfile: could not cat from %s", name);
}

void
writestring(char *dirname, char *file, char *string) {
	char *name;
	int wfd, amt;
	int slen = strlen(string);

	name = smprint("%s/%s", dirname, file);
	wfd = open(name, OWRITE);
	if (wfd < 0) {
		perror(name);
		threadexitsall(name);
	}
	debug(2, "Write %s to %s\n", string, name);

	amt = write(wfd, string, slen);
	if (amt < slen)
		perror(name);
	free(name);
	close(wfd);
}

void
openfiles(int f, char *s)
{
	char buf[20], *tmp;
	long n;
	memset(buf, 0, sizeof(buf));
	n = read(f, buf, (long)sizeof buf);
	if(n <= 0)
		sysfatal("can not clone %s: %r", s);

	debug(3, "in openfile fid: %d, buf: %s\n", f, buf);
	/* force it to be rooted ... */
	tmp = smprint("/%s/%s/xcpu/%s", base, s, buf);
	totalenvsize += strlen(tmp) + 1; 
	dirno[nodeno++]	= tmp;
	debug(3,"totales now %d\n", totalenvsize);	
}

void
threadmain(int argc, char *argv[])
{
	void shell(void);

	Dir *dir = 0;
	int f, i, d, n;

	if(argc == 1)
		threadexitsall("at least one");

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
	
	if(argc < 2) 
		usage();

	hostreg = regcomp(argv[0]);
	if(hostreg == nil)
		sysfatal("not a valid regular expression: %s: %r", argv[0]);

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
		threadexitsall("dirno malloc failed");

	debug(7, "found %d directories\n", d);
	for(i = 0; i < d; i++) {
		debug(2, "matching dir against regexp: %s\n", dir[i].name);
		if(regexec(hostreg, dir[i].name, nil, 0)) {
			debug(2, "found matching dir: %s\n", dir[i].name);
			dirno[nodecount++] = smprint("/%s/%s/xcpu", base, dir[i].name);
		}
	}
	free(dir);

	if(nodecount == 0)
		sysfatal("no matching nodes found in %s", base);

	binary = argv[0];

	/* copy the binary out */
	for(i = 0; i < nodecount; i++) {
		char *name, buf[100];

		if(i < 0 || i >= nodecount)
			threadexits("bad node id");
	
		name = smprint("%s/clone", dirno[i]);
		if(name == nil)
			sysfatal("smprint failed in dothread: %r");
		f = open(name, OREAD);
		if(f < 0)
			sysfatal("can't open %s: %r", name);
		n = read(f, buf, (long)sizeof buf);
		if(n <= 0)
			sysfatal("can not clone %s: %r", name);
		free(name);
		buf[n] = '\0';

		name = smprint("%s/%s", dirno[i], buf);
		proccreate(copybinary, (void *)name, Stack);
		/* do not free "name" here */
	}

	/* start a shell */
	//shell();
}


