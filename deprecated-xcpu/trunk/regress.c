/* regression tester for xcpusrv
 * derived from Plan9Ports' 9p.c
 */

#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9pclient.h>
#include <9p.h>

enum {
	Stack = 8192 * 2,
	Bufsize = 8192,
};

typedef struct Exec Exec;
struct Exec {
	CFsys *fs;
	char *bin;
	char *args;
};

char *addr, *path;

void
usage(void)
{
	fprint(2, "usage: %s [-n nw] dialstring\n", argv0);
	fprint(2, "\twhere nw is the number of workers to start simultaneously\n");
	fprint(2, "\tsample dialstring: tcp!127.0.0.1!20001\n");
	threadexitsall("usage");
}

void
dofile(CFsys *fs, char *file)
{
	char buf;
	CFid *fd;
	Dir *d;

	d = fsdirstat(fs, file);
	if(d == nil) {
		print("ERROR: dirstat(%s): %r\n", file);
		return;
	} else
		print("OK: dirstat(%s)\n", file);

	fd = fsopen(fs, file, OREAD);
	if(fd == nil) {
		if(d->mode & DMREAD)
			print("ERROR: open(%s, OREAD) failed but DMREAD set: %r\n");
		else
			print("OK: open(%s, OREAD): %r\n", file);
		goto doneread;
	} else
		print("OK: open(%s, OREAD)\n", file);

	/* stdout and stderr can hang. Just don't do I/O to std* */
	if (! strstr(file, "std")) {
		if((fsread(fd, &buf, 1) != 1) && (d->mode & DMREAD))
			print("ERROR: read(%s, 1): %r\n", file);
		else
			print("OK: read(%s, 1)\n", file);
	}
	fsclose(fd);

doneread:
	fd = fsopen(fs, file, OWRITE);
	if(fd == nil) {
		if(d->mode & DMWRITE)
			print("ERROR: open(%s, OWRITE) failed but DMWRITE set: %r\n", file);
		else
			print("OK: open(%s, OWRITE): %r\n", file);
		goto donewrite;
	} else
		print("OK: open(%s, OWRITE)\n", file);

	if(fswrite(fd, &buf, 1) != 1)
		print("ERROR: write(%s, 1): %r\n", file);
	else
		print("OK: write(%s, 1)\n", file);
	fsclose(fd);

donewrite:

	free(d);
	return;
}

void
dodir(CFsys *fs, char *name)
{
	Dir *dir;
	CFid *fd;
	char *fullname;
	int i, nd;
	print("%s: dir open: ", name);
	fd = fsopen(fs, name, OREAD);
	if(fd == nil)
		print("ERROR: fsopen(%s): %r\n", name);
	else
		print("OK: fsopen(%s)\n", name);

	nd = fsdirreadall(fd, &dir);
	if(nd <= 0)
		print("ERROR: dirreadall(%s): %r\n", name);
	else
		print("OK: dirreadall(%s); read %d files: ", name, nd);
	print("\t");
	for(i = 0; i < nd; i++)
		print("%s ", dir[i].name);
	print("\n");

	for(i = 0; i < nd; i++) {
		fullname = smprint("%s/%s", name, dir[i].name);

		if(dir[i].mode & DMDIR) {
			dodir(fs, fullname);
		} else {
			dofile(fs, fullname);
		}
		free(fullname);
	}
	free(dir);
	fsclose(fd);
}

void
copyto(int from, CFid *to) 
{
	int r, w;
	char *buf;

	buf = malloc(Bufsize);

	if (! buf)
		sysfatal("copyto: alloc fails: %r");

	while ((r = read(from, buf, Bufsize)) > 0) {
		w = fswrite(to, buf, r);
		if (w != r) {
			sysfatal("copyto: %d != %d: %r", r, w);
		}
	}
}

void
writestring(CFsys *fs, char *dirname, char *file, char *string) 
{
	CFid *wfd;
	char *name;
	int amt, slen = strlen(string);

	name = smprint("%s/%s", dirname, file);
	if(name == nil)
		sysfatal("writestring: name: %r");

	wfd = fsopen(fs, name, OWRITE);
	if (wfd == nil)
		sysfatal("writestring: wfd: %s: %r", name);

	amt = fswrite(wfd, string, slen);
	if (amt < slen)
		sysfatal("writestring: fswrite: %s: %r", name);

	fsclose(wfd);
}

void
copybinary(CFsys *fs, char *base, char *execname)
{
	CFid *wfd;
	char *name;
	int rfd;
	char *execpath = nil;

	/* the rule for now is that if it starts with a /, you take it as it is, otherwise you 
	  * prepend the uname path. 
	  */
	if (execname[0] == '/') {
		execpath = strdup(execname);
	} else {
		sysfatal("getarch unimplemented in copybinary");
	} 
	if(execpath == nil)
		sysfatal("execpath: %r");

	print("OK: copybinary: binary that will be copied is %s\n", execpath);
	rfd = open(execpath, OREAD);
	if (rfd < 0)
		sysfatal("rfd: %s: %r", execpath);
	print("OK: copybinary: opened binary, fd: %d\n", rfd);

	name = smprint("%s/exec", base);
	wfd = fsopen(fs, name, OWRITE);
	if (wfd == nil)
		print("ERROR: copybinary: opened binary, fd: %d\n", rfd);

	copyto(rfd, wfd);
	print("OK: copybinary: binary copied\n");

	close(rfd);
	fsclose(wfd);
	free(name);
	free(execpath);
}

void
runcommand(void *v)
{
	Exec *exe = (Exec *)v;
	CFsys *fs = exe->fs;
	CFid *fid;
	char *bin = exe->bin;
	char *args = exe->args;
	char buf[32];
	int n;

	fid = fsopen(exe->fs, "/clone", OREAD);
	if(fid == nil)
		print("ERROR: fsopen(/clone, OREAD): %r ");
	else {
		print("OK: fsopen(/clone, OREAD)\n");
		if((n = fsread(fid, buf, (long)sizeof buf)) <= 0) {
			print("ERROR: fsread(/clone, %d): %r\n", sizeof buf);
			threadexits("fsread"); 	/* no reason continuing */
		} else {
			buf[n] = '\0';
			print("OK: fsread(/clone, %d): session id: %s\n", sizeof buf, buf);
		}
	}

	copybinary(fs, buf, bin);
	print("OK: %s: copied binary\n", bin);

	if(args) {
		writestring(fs, buf, "argv", args);
		print("OK: %s: %s: wrote argv\n", bin, args);
	}

	writestring(fs, buf, "ctl", "exec");
	print("OK: %s: wrote exec to ctl\n", bin);

	fsclose(fid);

	threadexits(nil);
}

CFsys*
xparse(char *name, char **path)
{
	int fd;
	CFsys *fs;

	*path = name;
	if((fd = dial(addr, nil, nil, nil)) < 0)
		sysfatal("ERROR: dial: %r");
	if((fs = fsmount(fd, nil)) == nil)
		sysfatal("ERROR: fsamount: %r");

	return fs;
}

Exec exe[] = {
	nil, "/bin/date", nil,
	nil, "/bin/date", "-u", 
	nil, "/bin/date", "-s",
};

void
threadmain(int argc, char **argv)
{
	CFsys *fs;
	CFid **fid;
	Channel *c;
	char buf[32];
	int n, i, nw = 5;

	ARGBEGIN{
	case 'n':
		nw = strtoul(EARGF(usage()), nil, 0);
		break;
	default:
		usage();
	}ARGEND

	if(argc != 1)
		usage();

	addr = argv[0];

	fid = emalloc9p(nw * sizeof(CFid *));

	if(strchr(addr, '!') == nil)
		addr = netmkaddr(addr, "tcp", "20001");
	print("---------------attaching to server at %s-------------\n", addr);
	fs = xparse(addr, &path); 

	print("--------walking initial directory structure----------\n");
	dodir(fs, "/");
	print("------creating fake sessions: session one------------\n");
	fid[0] = fsopen(fs, "/clone", OREAD);
	if(fid[0] == nil)
		print("ERROR: fsopen(/clone, OREAD): %r ");
	else {
		print("OK: fsopen(/clone, OREAD)\n");
		if((n = fsread(fid[0], buf, (long)sizeof buf)) <= 0)
			print("ERROR: fsread(/clone, %d): %r\n", sizeof buf);
		else {
			buf[n] = '\0';
			print("OK: fsread(/clone, %d): session id: \n", sizeof buf, buf);
		}
	}

	print("------creating fake sessions: session two------------\n");
	fid[1] = fsopen(fs, "/clone", OREAD);
	if(fid[1] == nil)
		print("ERROR: fsopen(/clone, OREAD): %r ");
	else {
		print("OK: fsopen(/clone, OREAD)\n");
		if((n = fsread(fid[1], buf, (long)sizeof buf)) <= 0)
			print("ERROR: fsread(/clone, %d): %r\n", sizeof buf);
		else {
			buf[n] = '\0';
			print("OK: fsread(/clone, %d): session id: \n", sizeof buf, buf);
		}
	}


	print("--------walking new directory structure--------------\n");
	dodir(fs, "/");


	print("-------------closing fake sessions-------------------\n");
	fsclose(fid[0]);
	fsclose(fid[1]);

	/* now open nw sessions */
	print("-------------creating real sessions------------------\n");
	for(i = 0; i < nw; i++) {
		print("-------------session %d-----------------------\n", i);
		fid[i] = fsopen(fs, "/clone", OREAD);
		if(fid[i] == nil)
			print("ERROR: fsopen(/clone, OREAD): %r ");
		else {
			print("OK: fsopen(/clone, OREAD)\n");
			if((n = fsread(fid[i], buf, (long)sizeof buf)) <= 0)
				print("ERROR: fsread(/clone, %d): %r\n", sizeof buf);
			else {
				buf[n] = '\0';
				print("OK: fsread(/clone, %d): session id: %s\n", sizeof buf, buf);
			}
		}
	}
	print("--------walking new directory structure--------------\n");
	dodir(fs, "/");
	for(i = 0; i < nw; i++)
		fsclose(fid[i]);

	c = threadwaitchan();
	if(c == nil)
		sysfatal("ERROR: threadwaitchan(): %r");
	else
		print("OK: threadwaitchan()\n");

	/* 
	 * running commands in the new sessions happens in parralel
	 * the commands are:
	 * 	- /bin/date (stdout)
	 *	- /bin/date -u (argv)
	 * 	- /bin/date -s (stderr)
	 * 	- something with stdin???!
	 */
	print("-------------running commands------------------\n");
	for(i = 0; i < nw; i++) {
		int n;
		print("-------------session %d-----------------------\n", i);
		exe[0].fs = fs;
		exe[1].fs = fs;
		exe[2].fs = fs;
		n = proccreate(runcommand, &exe[0], Stack);
		if(n > 0)
			print("OK: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[0].bin, exe[0].args ? exe[0].args : "");
		else
			print("ERROR: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[0].bin, exe[0].args ? exe[0].args : "");

		n = proccreate(runcommand, &exe[1], Stack);
		if(n > 0)
			print("OK: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[1].bin, exe[1].args ? exe[1].args : "");
		else
			print("ERROR: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[1].bin, exe[1].args ? exe[1].args : "");

		n = proccreate(runcommand, &exe[2], Stack);
		if(n > 0)
			print("OK: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[2].bin, exe[2].args ? exe[2].args : "");
		else
			print("ERROR: proccreate: session %d, pid: %d, binary: %s, argv: %s\n", i, n, exe[2].bin, exe[2].args ? exe[2].args : "");
	}
}
