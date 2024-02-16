#include <stdio.h>
#include "libspu.h"

enum {
	Niter = 16*1024*1024,
	Stksize = 4096,
	Nsize = 8192,
//	Fsize = 134217728,
	Fsize = 128*1024*1024,
};

extern int spc_chatty;
int fd;
u8 buf[7][Nsize] __attribute__((aligned(128)));
char stack[7][Stksize] __attribute__((aligned(128)));
int nspu;
int id;

void readcor(void *a)
{
	int id, i, j, n;
	u64 len, offset;
	char *ename;
	u8 *buf;

	buf = a;
	len = 0;
	offset = (id * corid() - 1) * 2*1024*1024;
	for(i = 0; i < Niter; i++) {
//		memset(buf, 42, Nsize);
		if (offset > (Fsize - Nsize))
			offset = 0;

//		if ((id + j) % 2)
			n = spc_pread(fd, buf, Nsize, offset);
//		else
//			n = spc_pwrite(fd, buf, Nsize, offset);
			
//		fprintf(stderr, "i %d offset %lld count %d\n", i, offset, n);
		if (n != Nsize) {
			spc_log("offset %ld n %d\n", offset, n);
			if (n >= 0)
				sp_werror("Error while i/o");

			goto error;
		}

//		for(n = 0; n < Nsize; n++)
//			if (buf[n] != 0) {
//				sp_werror("internal error");
//				goto error;
//			}
//
//		if (i % 131072 == 131071)
//			spc_log(".");
		offset += n;
		len += n;
	}

	if (n < 0)
		goto error;

	spc_log("read %ld bytes\n", len);
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

        nspu = (int) (env >> 32);
        id = (int) (env & 0xFFFFFFFF);
//	fprintf(stderr, "start %llx\n", spuid);
	spc_chatty = 0;
	if (argv > 8) {
		sp_werror("argv should be less than 9");
		goto error;
	}

	fd = spc_open("#U/tmp/btest", Ordwr);
	if (fd < 0)
		goto error;

	for(i = 0; i < argv; i++) 
		mkcor(readcor, &buf[i], &stack[i], sizeof(stack[i]));

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}
