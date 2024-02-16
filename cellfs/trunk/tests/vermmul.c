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
#define MAXN		1024
#define PREF		"/tmp/mmul/"

float c[MAXN*MAXN];
float result[MAXN*MAXN];

int
main(int argc, char *argv[])
{
	int i, j, fd;
	int msize;
	float delta;

	fd = open(PREF"n", O_RDONLY);
	if (fd < 0)
		perror("n");

	read(fd, &msize, sizeof(msize));
	close(fd);

	fd = open(PREF"c", O_RDONLY);
	if (fd < 0)
		perror("c");

	read(fd, c, msize*msize*sizeof(float));
	close(fd);

	fd = open(PREF"result", O_RDONLY);
	if (fd < 0)
		perror("result");

	read(fd, result, msize*msize*sizeof(float));
	close(fd);

	for(i = 0; i < msize; i++)
		for(j = 0; j < msize; j++) {
			delta = result[i*msize + j] - c[i*msize + j];
			if (delta < 0.0)
				delta = -delta;

			if (delta > 0.01)
				fprintf(stderr, "%d %d c=%f result=%f delta %f\n", i, j, c[i*msize+j], result[i*msize+j], delta);
		}
	
	return 0;
}
