#include <stdio.h>
#include "libspu.h"

enum {
	Nreads = 64,
	Stksize = 4096,
	Nsize = 8192,
};

extern int spc_chatty;
int fd;
u8 buf[7][Nsize] __attribute__((aligned(128)));
char stack[7][Stksize] __attribute__((aligned(128)));

void readcor(void *a)
{
	int id, i, j, n, m;
	u64 len, offset;
	char *ename;
	u8 *buf;

	id = corid();
	buf = a;
	len = 0;
	for(i = 0; i < Nreads; i++) {
		offset = (corid() - 1) * 16 * 1024 * 1024;
		for(j = 0; j < 2048; j++) {
			m = 0;
			while (m < Nsize) {
				n = spc_pread(fd, buf + m, Nsize - m, offset);
				if (n < 0) 
					goto error;
				m += n;
				offset += n;
				len += n;
			}
		}
	}

	if (n < 0)
		goto error;

//	spc_log("read %lld bytes\n", len);
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

//	fprintf(stderr, "start %llx\n", spuid);
	spc_chatty = 0;
	if (argv > 8) {
		sp_werror("argv should be less than 9");
		goto error;
	}

	fd = spc_open("#u/tmp/stest", Ordwr);
	if (fd < 0)
		goto error;

	for(i = 0; i < argv; i++) 
		mkcor(readcor, &buf[i], &stack[i], sizeof(stack[i]));

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}
