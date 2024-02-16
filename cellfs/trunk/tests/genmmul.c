/* --------------------------------------------------------------  */
/* (C)Copyright 2001,2006,                                         */
/* International Business Machines Corporation,                    */
/* Sony Computer Entertainment, Incorporated,                      */
/* Toshiba Corporation,                                            */
/*                                                                 */
/* All Rights Reserved.                                            */
/* --------------------------------------------------------------  */
/* PROLOG END TAG zYx                                              */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <fenv.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <sched.h>


#define M		64	/* basic block size */
#define PREF		"/tmp/mmul/"

static inline float rand_0_to_1()
{
  union {
    float f;
    unsigned int ui;
  } val;

  val.ui = (rand() >> 8) | 0x3f800000;
  val.f -= 1.0f;

  return (val.f);
}

void
block_swizzle(float *mat, int size) 
{
	int i, j, k, l;
	float *ptr, *src, *dst;
	float *tmp;

	tmp = malloc(sizeof(float) * size * size);
	ptr = tmp;
	for(i=0; i<(size/M); i++) {
		for(j=0; j<(size/M); j++){
			for(k=0; k<M; k++) {
				for(l=0; l<M; l++) {
					*ptr++ = mat[i*(M*size)+j*M+k*size+l];
				}
			}
		}
	}

	src = tmp;
	dst = mat;
	for(i=0; i<size*size; i++) 
		*dst++ = *src++;

	free(tmp);
}

int
main(int argc, char *argv[])
{
	int i, j, k, fd;
	int msize, blocks;
	int niter;
	float *a, *b, *c, *res;

	niter = 10000;
	if (argc > 1)
		msize = strtol(argv[1], 0, 10);
	else 
		msize = M;

	blocks = (msize/M) * (msize/M);
	a = malloc(sizeof(float) * msize * msize);
	b = malloc(sizeof(float) * msize * msize);
	c = malloc(sizeof(float) * msize * msize);
	res = malloc(sizeof(float) * msize * msize);

	fprintf(stderr, "Generating matrices...\n");
	for(i = 0; i < msize; i++) {
		for(j = 0; j < msize; j++) {
			a[i*msize + j] = rand_0_to_1();
			b[i*msize + j] = rand_0_to_1();
			c[i*msize + j] = 0.0f;
		}
	}

	fprintf(stderr, "Calculating result...\n");
	for (i = 0; i < msize; i++) {
		fprintf(stderr, "\t%d\n", i);
		for (j = 0; j < msize; j++) {
			res[i*msize + j] = 0.0f;
			for (k=0; k<msize; k++) {
				res[i*msize+j] += a[i*msize+k] * b[k*msize+j];
			}
		}
	}

	/* Swizzle inputs matrices and expected results to correspond to tiling. */
	fprintf(stderr, "Swizzling the matirces...\n");
	block_swizzle(a, msize);
	block_swizzle(b, msize);
	block_swizzle(res, msize);

	fprintf(stderr, "Writing the files...\n");
	fd = open(PREF"a", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create a ");

	write(fd, a, sizeof(float) * msize * msize);
	close(fd);

	fd = open(PREF"b", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create b ");

	write(fd, b, sizeof(float) * msize * msize);
	close(fd);

	fd = open(PREF"c", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create c ");

	write(fd, c, sizeof(float) * msize * msize);
	close(fd);

	fd = open(PREF"result", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create result ");

	write(fd, res, sizeof(float) * msize * msize);
	close(fd);

	fd = open(PREF"n", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create result ");

	write(fd, &msize, sizeof(msize));
	close(fd);

	fd = open(PREF"niter", O_TRUNC | O_CREAT | O_RDWR, 0666);
	if (fd < 0)
		perror("cannot create result ");

	write(fd, &niter, sizeof(niter));
	close(fd);


	return 0;
}
