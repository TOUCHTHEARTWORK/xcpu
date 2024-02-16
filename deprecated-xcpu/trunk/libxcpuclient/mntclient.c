/*
 * library functions for xcpu clients using mounted compute nodes
 */
#include <u.h>
#include <libc.h>
#include <thread.h>
#include <bio.h>

#ifdef p9p
#include <libString.h>
#else
#include <String.h>
#endif

#include "mntclient.h"

String *
getarch(char *mnt)
{
	Biobuf *bio;
	String *arch;
	char *path;

	path = smprint("%s/%s", mnt, "uname");
	if(path == nil) {
		werrstr("getarch: error in smprint: %r");
		return nil;
	}

	bio = Bopen(path, OREAD);
	if (bio == nil) {
		werrstr("getarch: can't open uname file at %s: %r", path);
		free(path);
		return nil;
	}

	arch = s_new();
	if(arch == nil) {
		werrstr("getarch: can't open uname file at %s: %r", path);
		free(path);
		Bterm(bio);
		return nil;
	}

	if(s_getline(bio, arch) == nil) {
		werrstr("getarch: can't read file at %s: %r", path);
		free(path);
		Bterm(bio);
		return nil;
	}

	return arch;
}


int
copy(int from, int to)
{
	Ioproc *ior, *iow;
	int r, w, sum = 0;
	char *buf;

	buf = malloc(Bufsize);
	if(buf == nil)
		return -1;

	ior = ioproc();
	if(ior == nil)
		return -1;

	iow = ioproc();
	if(ior == nil || iow == nil) {
		closeioproc(ior);
		return -1;
	}

	while ((r = read(from, buf, Bufsize)) > 0) {
		w = write(to, buf, r);
		if (w != r) {
			werrstr("copy: write failed");
			closeioproc(ior);
			closeioproc(iow);
			return -1;
		}
		sum += w;
	}
	closeioproc(ior);
	closeioproc(iow);
	free(buf);
	return sum;
}

void
copybinary(char *mnt, char *session, char *binary)
{
	char *name;
	int rfd, wfd;
	char *execpath;

	/* the rule for now is that if it starts with a /, you take it as it is, otherwise you 
	  * prepend the uname path. 
	  */
	if (*binary == '/') {
		execpath = strdup(binary);
	} else {
		String *arch;
		arch = getarch(mnt);
		if(arch == nil)
			sysfatal("copybinary: getarch failed: %r");
		execpath = smprint("/%s/%s", s_to_c(arch), binary);
	} 
	if(execpath == nil)
		sysfatal("copybinary: execpath: %r");

	rfd = open(execpath, OREAD);
	if (rfd < 0)
		sysfatal("copybinary: can not open binary: %s: %r", execpath);

	name = smprint("%s/%s/exec", mnt, session);
	if(name == nil)
		sysfatal("copybinary: name: %r\n");
	wfd = open(name, OWRITE);
	if (wfd < 0)
		sysfatal("copybinary: can not open destination: %s: %r", name);

	if(copy(rfd, wfd) < 0)
		sysfatal("copybinary: could not copy binary to %s", name);

	close(rfd);
	close(wfd);
	free(name);
	free(execpath);
}

void
catfile(void *v)
{
	Xcpuio *x = (Xcpuio *)v;
	Ioproc *ior, *iow;
	char *name, *buf;
	int fd, n;

	buf = malloc(Bufsize);
	if(buf == nil)
		sysfatal("catfile: buf: %r");

	ior = ioproc();
	iow = ioproc();
	if(ior == nil || iow == nil)
		sysfatal("catfile: ioproc: %r");

	name = smprint("%s/%s/%s", x->mnt, x->session, x->name);
	if(name == nil)
		sysfatal("catfile: smprint failed: %r");

	fd = open(name, OREAD);
	if (fd < 0)
		sysfatal("catfile: can not open %s: %r", name);

	while((n = ioread(ior, fd, buf, Bufsize)) > 0)
		iowrite(iow, x->fd, buf, n);

	sendul(x->chan, x->fd);

	free(name);
	closeioproc(ior);
	closeioproc(iow);
}


int
writestring(char *mnt, char *file, char *string) 
{
	char *name;
	int wfd, amt;
	int slen = strlen(string);

	name = smprint("%s/%s", mnt, file);
	wfd = open(name, OWRITE);
	if (wfd < 0)
		sysfatal("writestring: can not open destination %s: %r", name);


	amt = write(wfd, string, slen);
	if (amt < slen)
		perror(name);

	free(name);
	close(wfd);
	return (amt < slen ? -1 : 0);
}

