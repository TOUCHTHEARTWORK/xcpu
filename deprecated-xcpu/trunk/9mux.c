/* 
 * mux connections to 9p servers
 *
 * derived from Plan9Ports' 9p.c
 */

#include <u.h>
#include <libc.h>
#include <fcall.h>
#include <thread.h>
#include <9pclient.h>
#include <9p.h>

char *addr, *path;

void
usage(void)
{
	print("usage: %s [-n nw] [tcp!]host[!port]\n", argv0);
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

void
threadmain(int argc, char **argv)
{
	CFsys *fs;
	CFid **fid;
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

	/* now open 5 sessions */
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

	threadexitsall(0);
}
