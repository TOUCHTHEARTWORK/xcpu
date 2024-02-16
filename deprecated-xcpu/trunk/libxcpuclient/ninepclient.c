/*
 * library functions for xcpu clients using mounted compute nodes
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>
#include <9pclient.h>

#ifdef p9p
#include <libString.h>
#else
#include <String.h>
#endif

#include "ninepclient.h"

String *
fsgetarch(CFsys *fs)
{
	String *arch;
	CFid *fd;
	char *path = "/uname";
	char buf[Namesize];
	int n;

	fd = fsopen(fs, path, OREAD);
	if (fd == nil) {
		werrstr("getarch: can't open uname file at %s: %r", path);
		return nil;
	}

	arch = s_new();
	if(arch == nil) {
		werrstr("getarch: can't open uname file at %s: %r", path);
		fsclose(fd);
		return nil;
	}

	if((n = fsread(fd, buf, sizeof (buf) - 1)) < 0) {
		werrstr("getarch: can't read file at %s: %r", path);
		fsclose(fd);
		return nil;
	}
	buf[n] = '\0';
	return s_copy(buf);
}

String *
fsclone(CFsys *fs)
{
	CFid *fid;
	char *buf;
	String *session = nil;
	int n;

	fid = fsopen(fs, "/clone", OREAD);
	if(fid == nil) {
		werrstr("fsclone: clone/: %r");
		return nil;
	}

	buf = mallocz(Namesize, 1);
	n = fsread(fid, buf,  Namesize);
	if(n <= 0) {
		werrstr("fsclone: read: %r");
		fsclose(fid);
		free(buf);
		return nil;
	}
	session = s_copy(buf);
	free(buf);
	return session;
}

int
fswritestring(CFsys *fs, char *mnt, char *file, char *string) 
{
	CFid *wfd;
	char *name;
	int amt, slen = strlen(string);

	name = smprint("%s/%s", mnt, file);
	if(name == nil) {
		werrstr("fswritestring: smprint: %r");
		return -1;
	}
	wfd = fsopen(fs, name, OWRITE);
	if(wfd == nil) {
		werrstr("fswritestring: wfd: %s: %r", name);
		return -1;
	}

	amt = fswrite(wfd, string, slen);
	if(amt < slen) {
		werrstr("fswritestring: fswrite: %s: %r", name);
		fsclose(wfd);
		return -1;
	}

	fsclose(wfd);
	return amt;
}

/* copy to a remote server over 9p */
int
fscopyto(int from, CFid *to) 
{
	int r, w, sum = 0;
	char *buf;

	buf = malloc(Bufsize);
	if(buf == nil) {
		werrstr("fscopyto: malloc: %r");
		return -1;
	}

	while ((r = read(from, buf, Bufsize)) > 0) {
		w = fswrite(to, buf, r);
		if (w != r)
			return -1;
		sum += w;
	}
	return sum;
}

/* copy from a remote server over 9p */
int
fscopyfrom(CFid *from, int to) 
{
	char *buf;
	int r, w, sum = 0;

	buf = malloc(Bufsize);
	if(buf == nil) {
		werrstr("fscopyto: malloc: %r");
		return -1;
	}

	while ((r = fsread(from, buf, Bufsize)) > 0) {
		w = write(to, buf, r);
		if (w != r) 
			return -1;
		sum += w;
	}
	return sum;
}

int
fscatfile(void *v)
{
	Xcpuio *x = (Xcpuio *)v;
	CFid *fid;
	char *name;

	name = smprint("/%s/%s", x->session, x->name);

	fid = fsopen(x->fs, name, OREAD);
	if (fid == nil) {
		werrstr("fscatfile: can not open %s: %r", name);
		return -1;
	}

	fscopyfrom(fid, x->fd);
	free(name);
	fsclose(fid);
	send(x->chan, 0);
	return 0;
}

int
fscopybinary(CFsys *fs, char *base, char *execname)
{
	CFid *wfd;
	String *arch = nil;
	char *name, *execpath;
	int rfd, amt;

	/* the rule for now is that if it starts with a /, you take it as it is, otherwise you 
	  * prepend the uname path. 
	  */
	if (execname[0] == '/') {
		execpath = strdup(execname);
	} else {
		arch = fsgetarch(fs);
		if(arch == 0) {
			werrstr("fscopybinary: fsgetarch: %r");
			return -1;
		}
		execpath = smprint("/%s/%s", s_to_c(arch), execname);
	} 
	if(execpath == nil) {
		werrstr("fscopybinary: execpath: %r");
		return -1;
	}

	rfd = open(execpath, OREAD);
	if (rfd < 0) {
		werrstr("fscopybinary: rfd: %s: %r", execpath);
		return -1;
	}

	name = smprint("%s/exec", base);
	wfd = fsopen(fs, name, OWRITE);
	if (wfd == nil) {
		werrstr("fscopybinary: wfid: %s: %r", name);
		return -1;
	}

	amt = fscopyto(rfd, wfd);

	close(rfd);
	fsclose(wfd);
	free(name);
	free(execpath);
	s_free(arch);

	return amt;
}

