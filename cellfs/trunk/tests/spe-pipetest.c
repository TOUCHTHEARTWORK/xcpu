#include <stdio.h>
#include "libspu.h"

enum {
	Nreads = 8192,
	Stksize = 16384,
};

extern int spc_chatty;
int fdin, fdout;

u8 buf1[8192] __attribute__((aligned(128)));
u8 buf2[8192] __attribute__((aligned(128)));
char stack1[Stksize] __attribute__((aligned(128)));
char stack2[Stksize] __attribute__((aligned(128)));

void
readcor(void *a)
{
	int fd, n;
	char *ename;

	n = spc_read(fdin, buf2, sizeof(buf2));
	if (n < 0)
		goto error;

	buf2[n] = '\0';
	fd = spc_create("#u/tmp/sout", 0666, Owrite);
	spc_write(fd, buf2, n);
	spc_close(fd);
	spc_log("%d: Got: %s", corid(), buf2);
	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}

void
writecor(void *a)
{
	int n;
	char *ename;

//	sprintf(buf1, "hello world\n");
	n = spc_write(fdout, buf1, strlen(buf1));
	if (n != strlen(buf1))
		goto error;

//	fprintf(stderr, "%d: wrote %d bytes\n", corid(), n);
	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}

void
cormain(unsigned long long spuid, unsigned long long argv, unsigned long long env)
{
	int i, n;
	char *ename;

	spc_chatty = 1;
	fdout = spc_create("#p/pip", 0666, Owrite);
	if (fdout < 0)
		goto error;

	fdin = spc_open("#p/pip", Oread);
	if (fdin < 0)
		goto error;

	mkcor(readcor, NULL, &stack1, sizeof(stack1));
	mkcor(writecor, NULL, &stack2, sizeof(stack2));

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}
