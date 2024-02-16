#include <stdio.h>
#include "libspu.h"

enum {
	Filesize = 100 * 1024 * 1024,
	Nreads = 1000,
	Stksize = 8192,
	Dmasize = 1024*16,
	NumCoroutines,
};

extern int spc_chatty;
char stack[NumCoroutines][Stksize] __attribute__((aligned(128)));
char fname[128];

typedef struct Data Data;
struct Data {
	unsigned long long offset;
	unsigned long long size;
};

Data	data[NumCoroutines];

void
r(void *a)
{
	Data *d = a;
	char arr[Dmasize];
	unsigned long i;

	fd = spc_open(fname, Oread);
	if (fd < 0)
		goto error;

	for(i = d->offset; i < d->offset+d->size; i += DmaSize) {
		n = spc_read(fd, arr, Dmasize);
		if(n < 0) 
			goto error;
	}
	spc_close(fd);
	fprintf(stderr, "done %d\n", corid());

error:
	sp_rerror(&ename);
	fprintf(stderr, "Error: %s\n", ename);
}

void
cormain(unsigned long long spuid, unsigned long long argv)
{
	int i, n, num = NumCoroutines;
	unsigned long long size, offset;
	char *ename;

	spc_chatty = 0;

	sprintf(fname, "#r/moo-%lld", spuid);	/* global */

	/* need to be able to send an offset and a size to the coroutine */
	size = argsize/num;	/* how much each coroutine needs to gobble */
	offset = argoffset;	/* as with argsize, there should be a way of passing this from the ppu */

	/* bug: the size given to us must be divisible by the number of coroutines
	 * or this code won't finish the reminder. perhaps each coroutine can 
	 * be given an equal amount of it until the remainder is 0
	 */


	switch(num) {
	case 8:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 7:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 6:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 5:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 4:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 3:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 2:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	case 1:
		data[num].size = size;
		data[num].offset = offset + size * (num-1);
		mkcor(r, data[num], stack[num], sizeof(stack[num]));
		num--;
	default:
		sp_werror("illegal # of coroutines: must be between 1 and 8\n");
	}

	return;

error:
	sp_rerror(&ename);
	fprintf(stderr, "Error: %s %d\n", ename);
}
