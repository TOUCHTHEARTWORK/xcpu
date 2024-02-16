#include <stdio.h>
#include <spu_intrinsics.h>
#include "libspu.h"

#define PREF "#R/tmp/sgemv/"
#define NELEM(x) (sizeof(x)/(sizeof(x[0])))

enum {
	Ncor = 6,
	Nsize = 4096,
};

typedef struct Ctx Ctx;
struct Ctx {
	int	r0;
	float*	a;
};

int afd;
int yfd;

int id;
int nspu;
int ncor;
int rpspu;
int rpcor;

float alpha = 3.4;
float beta = 0.4;
float x[Nsize] __attribute__((aligned(128)));
float y[Nsize] __attribute__((aligned(128)));
float ac[Ncor][Nsize] __attribute__((aligned(128)));
Ctx ctx[Ncor];
char stack[Ncor][4*1024] __attribute__((aligned(128)));

void
sgemvcor(void *a)
{
	int i, j, n, l;
	u64 of;
	float *vy;
	vector float yv;
	Ctx *ctx;

	ctx = a;
	vy = (float *) &yv;
	for(l = 0; l < 1000; l++) {
	for(i = 0; i < rpcor; i++) {
		of = (ctx->r0 + i) * Nsize * sizeof(float);
//		spc_log("%d: calculate y[%d]\n", id, ctx->r0 + i);
//		spc_log("spc_pread %d:%d offset %lx\n", id, corid(), of);
		n = spc_pread(afd, (u8 *) ctx->a, Nsize*sizeof(float), of);
		yv = spu_splats(0.0f);
		for(j = 0; j < Nsize/4; j++)
			yv = spu_madd(*(vector float *) &ctx->a[j*4], *(vector float *) &x[j*4], yv);

		y[ctx->r0 + i] *= beta;
		y[ctx->r0 + i] += alpha*(vy[0]+vy[1]+vy[2]);

/*
		yv = alpha * y[ctx->r0 + i];
		for(j = 0; j < Nsize; j++)
			yv += ctx->a[j] * x[j];

		y[ctx->r0 + i] = yv;
*/
	}

	spc_pwrite(yfd, (u8 *) &y[ctx->r0], rpcor * sizeof(float), ctx->r0 * sizeof(float));
	}
}

void
cormain(unsigned long long spuid, unsigned long long arg, unsigned long long env)
{
	int i, n, r;
	int fd = -1;
	char *ename;

	ncor = (int) arg;
	if (ncor == 0)
		ncor = 1;

	nspu = (int) (env >> 32);
	id = (int) (env & 0xFFFFFFFF);

//	for(i = 0; i < 1000; i++) {
	if (fd != -1)
		spc_close(fd);

	fd = spc_open(PREF"x", Oread);
	if (fd < 0)
		goto error;

	n = spc_read(fd, (u8 *) x, sizeof(x));
	if (n != sizeof(x))
		goto error;
	spc_close(fd);

	yfd = spc_open("#R/tmp/sgemv/y", Ordwr);
	if (yfd < 0)
		goto error;

	n = spc_read(yfd, (u8 *) y, sizeof(y));
	if (n != sizeof(y))
		goto error;
//	}

	afd = spc_open(PREF"a", Oread);
	if (afd < 0)
		goto error;

	rpspu = Nsize / nspu;
	rpcor = rpspu / ncor;
	r = id * rpspu;

//	spc_log("rows-per-spu %d rows-per-cor %d\n", rpspu, rpcor);
	for(i = 0; i < ncor; i++) {
		ctx[i].r0 = r + i*rpcor;
		ctx[i].a = &ac[i][0];
		if (mkcor(sgemvcor, &ctx[i], &stack[i], sizeof(stack[i])) < 0)
			goto error;
	}

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}
