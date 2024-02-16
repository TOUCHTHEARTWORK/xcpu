#include <stdio.h>
#include "libspu.h"

enum {
	Filesize = 1 * 1024 * 1024,
	Nreads = 10,
	Stksize = 8192,
};

extern int spc_chatty;
u8 data[8192] __attribute__((aligned(128)));
char stack[6][Stksize] __attribute__((aligned(128)));
char fname[128];

void rtest(void *a)
{
	int i, n;
	char *name, *ename;
	int fd;

	name = a;
	fd = spc_open(name, Oread);
	if (fd < 0)
		goto error;

	while (n = spc_read(fd, data, sizeof(data)) > 0)
		;

	if (n < 0)
		goto error;

	spc_close(fd);
	return;

error:
	sp_rerror(&ename);
	fprintf(stderr, "Error: %s\n", ename);
}

void
r(void *a)
{
	int i;

	for(i = 0; i < Nreads; i++) {
		rtest(a);
	}

	spc_log("done %d\n", corid());
}

void wtest(void *a)
{
	int i, n;
	char *name, *ename;
	int fd;

	name = a;
//	fd = spc_create(name, 0666, Ordwr);
	fd = spc_open(name, Ordwr);
	if (fd < 0)
		goto error;

	for(i = 0; i < Filesize/sizeof(data); i++) {
		n = spc_write(fd, data, sizeof(data));
	}

	spc_close(fd);
	return;

error:
	sp_rerror(&ename);
	fprintf(stderr, "Error: %s %d\n", ename);
}

void
cormain(unsigned long long spuid, unsigned long long argv, unsigned long long env)
{
	int i, n;
	char *ename;

	spc_chatty = 0;
//	if (spc_mount("", "lionkov", -1) < 0)
//		goto error;

	memset(data, 0x31, sizeof(data));

	sprintf(fname, "#U/tmp/moo-%lld", spuid);
	wtest(fname);
	for(i = 0; i < argv; i++) 
		mkcor(r, fname, &stack[i], sizeof(stack[i]));

//	mkcor(r, fname, stack2, sizeof(stack2));
//	mkcor(r, fname, stack3, sizeof(stack3));
//	mkcor(r, fname, stack4, sizeof(stack4));
//	mkcor(r, fname, stack5, sizeof(stack5));
//	spc_remove("moo");

	return;

error:
	sp_rerror(&ename);
	fprintf(stderr, "Error: %s %d\n", ename);
}
