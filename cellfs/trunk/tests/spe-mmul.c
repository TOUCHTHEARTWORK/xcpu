/* --------------------------------------------------------------  */
/* (C)Copyright 2001,2006,                                         */
/* International Business Machines Corporation,                    */
/* Sony Computer Entertainment, Incorporated,                      */
/* Toshiba Corporation,                                            */
/*                                                                 */
/* All Rights Reserved.                                            */
/* --------------------------------------------------------------  */
/* PROLOG END TAG zYx                                              */

/*
 * Matrix Multiply --- EUC
 *
 * block.c - Matrix Multiply with block partitioning
 */

#include <spu_intrinsics.h>
// #include <vec_literal.h>
#include "libspu.h"

#define VEC_LITERAL(_type, ...)	((_type){__VA_ARGS__})

#ifndef M
#define M      		64		/* Size of the matrix block - M x M */
#endif


#define PREF "#U/tmp/mmul/"
#define MAX_COR		2
#define MAX_N		1024
#define MAX_TILES	(MAX_N / M)
#define STACKSIZE	4096

typedef struct Ctx Ctx;
struct Ctx {
	int	blkfirst;
	int	blklast;
	float	*a;
	float	*b;
	float	*c;
};

Ctx ctx[MAX_COR];
float a[MAX_COR*M*M] __attribute__((aligned(128)));
float b[MAX_COR*M*M] __attribute__((aligned(128)));
float c[MAX_COR*M*M] __attribute__((aligned(128)));
char stack[MAX_COR * STACKSIZE] __attribute__((aligned(128)));

static unsigned int N;
static unsigned int ITER;
static int id;
static int nspu;
static int bpspu;
static int ncor;
static int bpcor;
static int afd;
static int bfd;
static int cfd;
static int shift;
static int mask;

const vector unsigned char pat0 = VEC_LITERAL(const vector unsigned char,
						0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03,
						0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x03);

const vector unsigned char pat1 = VEC_LITERAL(const vector unsigned char,
						0x04, 0x05, 0x06, 0x07, 0x04, 0x05, 0x06, 0x07,
						0x04, 0x05, 0x06, 0x07, 0x04, 0x05, 0x06, 0x07);

const vector unsigned char pat2 = VEC_LITERAL(const vector unsigned char,
						0x08, 0x09, 0x0a, 0x0b, 0x08, 0x09, 0x0a, 0x0b,
						0x08, 0x09, 0x0a, 0x0b, 0x08, 0x09, 0x0a, 0x0b);

const vector unsigned char pat3 = VEC_LITERAL(const vector unsigned char,
						0x0c, 0x0d, 0x0e, 0x0f, 0x0c, 0x0d, 0x0e, 0x0f,
						0x0c, 0x0d, 0x0e, 0x0f, 0x0c, 0x0d, 0x0e, 0x0f);


static void MatInit_MxM(volatile float *blkC, volatile float *blkA, volatile float *blkB);
static void MatMult_MxM(volatile float *blkC, volatile float *blkA, volatile float *blkB);
static void mmulcor(void *a);

void
cormain(unsigned long long spuid, unsigned long long arg, unsigned long long env)
{
	int i, id, fd, tiles, nblocks, bfirst;
	float dt;
	char *ename;

	ncor = 2;
	nspu = (int) (env >> 32);
	id = (int) (env & 0xFFFFFFFF);
	afd = spc_open(PREF"a", Ordwr);
	if (afd < 0)
		goto error;

	bfd = spc_open(PREF"b", Ordwr);
	if (bfd < 0)
		goto error;

	cfd = spc_open(PREF"c", Ordwr);
	if (cfd < 0)
		goto error;

	fd = spc_open(PREF"n", Oread);
	if (fd < 0)
		goto error;

	if (spc_read(fd, (u8 *) &N, sizeof(N)) < sizeof(N))
		goto error;
	close(fd);

	fd = spc_open(PREF"niter", Oread);
	if (fd < 0)
		goto error;

	if (spc_read(fd, (u8 *) &ITER, sizeof(ITER)) < sizeof(ITER))
		goto error;
	close(fd);

	tiles = N / M;
	nblocks = tiles * tiles;
	mask = tiles - 1;
	shift = 32 - spu_extract(spu_cntlz(spu_promote(mask, 0)), 0);
	bpspu = nblocks / nspu;
	bpcor = bpspu / ncor;
	if (bpcor < 1)
		bpcor = 1;

	bfirst = id * bpspu;

//	spc_log("id %d nspu %d bpspu %d bpcor %d niter %d\n", id, nspu, bpspu, bpcor, ITER);
	for(i = 0; i < ncor; i++) {
		ctx[i].blkfirst = bfirst + i*bpcor;
		ctx[i].blklast = ctx[i].blkfirst + bpcor;
		if (ctx[i].blklast > nblocks)
			break;

		ctx[i].a = &a[i*M*M];
		ctx[i].b = &b[i*M*M];
		ctx[i].c = &c[i*M*M];

		if (mkcor(mmulcor, &ctx[i], &stack[i*STACKSIZE], STACKSIZE) < 0)
			goto error;
	}
/*
	ctx[0].blkfirst = bfirst;
	ctx[0].blklast = ctx[0].blkfirst + bpspu;
	ctx[0].a = &a[0*M*M];
	ctx[0].b = &b[0*M*M];
	ctx[0].c = &c[0*M*M];
	mmulcor(&ctx[0]);
*/

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}

static int
block_read(Ctx *ctx, unsigned int by, unsigned int bx, unsigned int idx)
{
	int err, sz;
	u64 offa, offb;

	offa = 4*(by*M*N + idx*M*M);
	offb = 4*(bx*M*M + idx*M*N);
	sz = sizeof(float) * M * M;

	err = spc_pread(afd, (u8 *) ctx->a, sz, offa);
	if (err != sz)
		return -1;

	err = spc_pread(bfd, (u8 *) ctx->b, sz, offb);
	if (err != sz)
		return -1;

	return 0;
}

static int
block_write(Ctx *ctx, unsigned int by, unsigned int bx)
{
	int err, sz;
	u64 offc;

	sz = sizeof(float) * M * M;
	offc = 4 * (by*M*N + bx*M*M);
	if (spc_pwrite(cfd, (u8 *) ctx->c, sz, offc) != sz)
		return -1;

	return 0;
}

static void
mmulcor(void *a)
{
	int i, j, k, blkid, iter;
	char *ename;
	Ctx *ctx;

	ctx = a;
//	spc_log("mmulcor %d\n", corid());
	for(iter = 0; iter < ITER; iter++) {
		for(blkid = ctx->blkfirst; blkid < ctx->blklast; blkid++) {
//			spc_log("blkid %d\n", blkid);
//			memset(ctx->c, 0, sizeof(float) * M * M);
			i = (blkid >> shift) & mask;
			j = (blkid) & mask;

			if (block_read(ctx, i, j, 0) < 0)
				goto error;

//			spc_log("matinit\n");
			MatInit_MxM(ctx->c, ctx->a, ctx->b);
			for(k = 1; k < (N/M); k++) {
				if (block_read(ctx, i, j, k) < 0)
					goto error;

				MatMult_MxM(ctx->c, ctx->a, ctx->b);
			}

			if (iter+1 == ITER)
				if (block_write(ctx, i, j) < 0)
					goto error;
		}
	}

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}

#ifdef USE_INLINE_ASM

#define ALIGN8B 	asm volatile(".align 3")

#define SPU_FMA(RT,RA,RB,RC)						\
  asm volatile(".align 3");						\
  asm volatile("fma %0,%1,%2,%3":"=r"(RT):"r"(RA),"r"(RB),"r"(RC))

#define SPU_FM(RT,RA,RB)				\
  asm volatile(".align 3");				\
  asm volatile("fm %0,%1,%2":"=r"(RT):"r"(RA),"r"(RB))

#define SPU_LNOP	asm volatile("lnop")

#else

#define ALIGN8B 
#define SPU_FMA(RT,RA,RB,RC) 	RT = spu_madd(RA, RB, RC)
#define SPU_FM(RT,RA,RB) 	RT = spu_mul(RA, RB)
#define SPU_LNOP

#endif



#define StageCBAclr(OFFSET)									\
{												\
  ALIGN8B;											\
  SPU_FM(c0_0B,a00,b0_0B);									\
  SPU_FM(c1_0B,a10,b0_0B);									\
  SPU_FM(c2_0B,a20,b0_0B);									\
  SPU_FM(c3_0B,a30,b0_0B);									\
  SPU_FM(c0_1B,a00,b0_1B);									\
  SPU_FM(c1_1B,a10,b0_1B);									\
  SPU_FM(c2_1B,a20,b0_1B);									\
  SPU_FM(c3_1B,a30,b0_1B);									\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B);								\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B);								\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); b0_0C = *((volatile vector float *)(ptrB+OFFSET+16));		\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); SPU_LNOP;							\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); b1_0C = *((volatile vector float *)(ptrB+M+OFFSET+16));	\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); b2_0C = *((volatile vector float *)(ptrB+2*M+OFFSET+16));	\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); b3_0C = *((volatile vector float *)(ptrB+3*M+OFFSET+16));	\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); SPU_LNOP;							\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); b0_1C = *((volatile vector float *)(ptrB+OFFSET+20));		\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); b1_1C = *((volatile vector float *)(ptrB+M+OFFSET+20));	\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); b2_1C = *((volatile vector float *)(ptrB+2*M+OFFSET+20));	\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); SPU_LNOP;							\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); b3_1C = *((volatile vector float *)(ptrB+3*M+OFFSET+20));	\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); *((volatile vector float *)(ptrC+OFFSET)) = c0_0A;		\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0A;		\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); SPU_LNOP;							\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0A;	\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0A;	\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1A;		\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); SPU_LNOP;							\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1A;	\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1A;	\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1A;	\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); SPU_LNOP;							\
}

#define StageACBclr(OFFSET)									\
{												\
  SPU_FM(c0_0C,a00,b0_0C);									\
  SPU_FM(c1_0C,a10,b0_0C);									\
  SPU_FM(c2_0C,a20,b0_0C);									\
  SPU_FM(c3_0C,a30,b0_0C);									\
  SPU_FM(c0_1C,a00,b0_1C);									\
  SPU_FM(c1_1C,a10,b0_1C);									\
  SPU_FM(c2_1C,a20,b0_1C);									\
  SPU_FM(c3_1C,a30,b0_1C);									\
  SPU_FMA(c0_0C,a01,b1_0C,c0_0C);								\
  SPU_FMA(c1_0C,a11,b1_0C,c1_0C);								\
  SPU_FMA(c2_0C,a21,b1_0C,c2_0C); b0_0A = *((volatile vector float *)(ptrB+OFFSET+16));		\
  SPU_FMA(c3_0C,a31,b1_0C,c3_0C); SPU_LNOP;							\
  SPU_FMA(c0_1C,a01,b1_1C,c0_1C); b1_0A = *((volatile vector float *)(ptrB+M+OFFSET+16));	\
  SPU_FMA(c1_1C,a11,b1_1C,c1_1C); b2_0A = *((volatile vector float *)(ptrB+2*M+OFFSET+16));	\
  SPU_FMA(c2_1C,a21,b1_1C,c2_1C); b3_0A = *((volatile vector float *)(ptrB+3*M+OFFSET+16));	\
  SPU_FMA(c3_1C,a31,b1_1C,c3_1C); SPU_LNOP;							\
  SPU_FMA(c0_0C,a02,b2_0C,c0_0C); b0_1A = *((volatile vector float *)(ptrB+OFFSET+20));		\
  SPU_FMA(c1_0C,a12,b2_0C,c1_0C); b1_1A = *((volatile vector float *)(ptrB+M+OFFSET+20));	\
  SPU_FMA(c2_0C,a22,b2_0C,c2_0C); b2_1A = *((volatile vector float *)(ptrB+2*M+OFFSET+20));	\
  SPU_FMA(c3_0C,a32,b2_0C,c3_0C); SPU_LNOP;							\
  SPU_FMA(c0_1C,a02,b2_1C,c0_1C); b3_1A = *((volatile vector float *)(ptrB+3*M+OFFSET+20));	\
  SPU_FMA(c1_1C,a12,b2_1C,c1_1C); *((volatile vector float *)(ptrC+OFFSET)) = c0_0B;		\
  SPU_FMA(c2_1C,a22,b2_1C,c2_1C); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0B;		\
  SPU_FMA(c3_1C,a32,b2_1C,c3_1C); SPU_LNOP;							\
  SPU_FMA(c0_0C,a03,b3_0C,c0_0C); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0B;	\
  SPU_FMA(c1_0C,a13,b3_0C,c1_0C); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0B;	\
  SPU_FMA(c2_0C,a23,b3_0C,c2_0C); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1B;		\
  SPU_FMA(c3_0C,a33,b3_0C,c3_0C); SPU_LNOP;							\
  SPU_FMA(c0_1C,a03,b3_1C,c0_1C); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1B;	\
  SPU_FMA(c1_1C,a13,b3_1C,c1_1C); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1B;	\
  SPU_FMA(c2_1C,a23,b3_1C,c2_1C); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1B;	\
  SPU_FMA(c3_1C,a33,b3_1C,c3_1C); SPU_LNOP;							\
}

#define StageBACclr(OFFSET)									\
{												\
  SPU_FM(c0_0A,a00,b0_0A);									\
  SPU_FM(c1_0A,a10,b0_0A);									\
  SPU_FM(c2_0A,a20,b0_0A);									\
  SPU_FM(c3_0A,a30,b0_0A);									\
  SPU_FM(c0_1A,a00,b0_1A);									\
  SPU_FM(c1_1A,a10,b0_1A);									\
  SPU_FM(c2_1A,a20,b0_1A);									\
  SPU_FM(c3_1A,a30,b0_1A);									\
  SPU_FMA(c0_0A,a01,b1_0A,c0_0A);								\
  SPU_FMA(c1_0A,a11,b1_0A,c1_0A);								\
  SPU_FMA(c2_0A,a21,b1_0A,c2_0A); b0_0B = *((volatile vector float *)(ptrB+OFFSET+16));		\
  SPU_FMA(c3_0A,a31,b1_0A,c3_0A); SPU_LNOP;							\
  SPU_FMA(c0_1A,a01,b1_1A,c0_1A); b1_0B = *((volatile vector float *)(ptrB+M+OFFSET+16));	\
  SPU_FMA(c1_1A,a11,b1_1A,c1_1A); b2_0B = *((volatile vector float *)(ptrB+2*M+OFFSET+16));	\
  SPU_FMA(c2_1A,a21,b1_1A,c2_1A); b3_0B = *((volatile vector float *)(ptrB+3*M+OFFSET+16));	\
  SPU_FMA(c3_1A,a31,b1_1A,c3_1A); SPU_LNOP;							\
  SPU_FMA(c0_0A,a02,b2_0A,c0_0A); b0_1B = *((volatile vector float *)(ptrB+OFFSET+20));		\
  SPU_FMA(c1_0A,a12,b2_0A,c1_0A); b1_1B = *((volatile vector float *)(ptrB+M+OFFSET+20));	\
  SPU_FMA(c2_0A,a22,b2_0A,c2_0A); b2_1B = *((volatile vector float *)(ptrB+2*M+OFFSET+20));	\
  SPU_FMA(c3_0A,a32,b2_0A,c3_0A); SPU_LNOP;							\
  SPU_FMA(c0_1A,a02,b2_1A,c0_1A); b3_1B = *((volatile vector float *)(ptrB+3*M+OFFSET+20));	\
  SPU_FMA(c1_1A,a12,b2_1A,c1_1A); *((volatile vector float *)(ptrC+OFFSET)) = c0_0C;		\
  SPU_FMA(c2_1A,a22,b2_1A,c2_1A); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0C;		\
  SPU_FMA(c3_1A,a32,b2_1A,c3_1A); SPU_LNOP;							\
  SPU_FMA(c0_0A,a03,b3_0A,c0_0A); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0C;	\
  SPU_FMA(c1_0A,a13,b3_0A,c1_0A); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0C;	\
  SPU_FMA(c2_0A,a23,b3_0A,c2_0A); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1C;		\
  SPU_FMA(c3_0A,a33,b3_0A,c3_0A); SPU_LNOP;							\
  SPU_FMA(c0_1A,a03,b3_1A,c0_1A); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1C;	\
  SPU_FMA(c1_1A,a13,b3_1A,c1_1A); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1C;	\
  SPU_FMA(c2_1A,a23,b3_1A,c2_1A); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1C;	\
  SPU_FMA(c3_1A,a33,b3_1A,c3_1A); SPU_LNOP;							\
}

#define StageCBA(OFFSET,OFFB)										\
{													\
  ALIGN8B;												\
  SPU_FMA(c0_0B,a00,b0_0B,c0_0B); c0_0C = *((volatile vector float *)(ptrC+OFFSET+16));			\
  SPU_FMA(c1_0B,a10,b0_0B,c1_0B); c1_0C = *((volatile vector float *)(ptrC+M+OFFSET+16));		\
  SPU_FMA(c2_0B,a20,b0_0B,c2_0B); c2_0C = *((volatile vector float *)(ptrC+2*M+OFFSET+16));		\
  SPU_FMA(c3_0B,a30,b0_0B,c3_0B); SPU_LNOP;								\
  SPU_FMA(c0_1B,a00,b0_1B,c0_1B); c3_0C = *((volatile vector float *)(ptrC+3*M+OFFSET+16));		\
  SPU_FMA(c1_1B,a10,b0_1B,c1_1B); c0_1C = *((volatile vector float *)(ptrC+OFFSET+20));			\
  SPU_FMA(c2_1B,a20,b0_1B,c2_1B); c1_1C = *((volatile vector float *)(ptrC+M+OFFSET+20));		\
  SPU_FMA(c3_1B,a30,b0_1B,c3_1B); SPU_LNOP;								\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B); c2_1C = *((volatile vector float *)(ptrC+2*M+OFFSET+20));		\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B); c3_1C = *((volatile vector float *)(ptrC+3*M+OFFSET+20));		\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); b0_0C = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+16));		\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); SPU_LNOP;								\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); b1_0C = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); b2_0C = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); b3_0C = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); SPU_LNOP;								\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); b0_1C = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+20));		\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); b1_1C = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); b2_1C = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); SPU_LNOP;								\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); b3_1C = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); *((volatile vector float *)(ptrC+OFFSET)) = c0_0A;			\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0A;			\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); SPU_LNOP;								\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0A;		\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0A;		\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1A;			\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); SPU_LNOP;								\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1A;		\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1A;		\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1A;		\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); SPU_LNOP;								\
}

#define StageCBAmod(OFFSET,OFFB)									\
{													\
  ALIGN8B;												\
  SPU_FMA(c0_0B,a00,b0_0B,c0_0B); SPU_LNOP;								\
  SPU_FMA(c1_0B,a10,b0_0B,c1_0B); b2_0B = *((volatile vector float *)(ptrB+2*M+OFFB*M+8));		\
  SPU_FMA(c2_0B,a20,b0_0B,c2_0B); b2_1B = *((volatile vector float *)(ptrB+2*M+OFFB*M+12));		\
  SPU_FMA(c3_0B,a30,b0_0B,c3_0B); b3_0B = *((volatile vector float *)(ptrB+3*M+OFFB*M+8));		\
  SPU_FMA(c0_1B,a00,b0_1B,c0_1B); b3_1B = *((volatile vector float *)(ptrB+3*M+OFFB*M+12));		\
  SPU_FMA(c1_1B,a10,b0_1B,c1_1B); c0_0C = *((volatile vector float *)(ptrC+OFFSET+16));			\
  SPU_FMA(c2_1B,a20,b0_1B,c2_1B); c1_0C = *((volatile vector float *)(ptrC+M+OFFSET+16));		\
  SPU_FMA(c3_1B,a30,b0_1B,c3_1B); c2_0C = *((volatile vector float *)(ptrC+2*M+OFFSET+16));		\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B); c3_0C = *((volatile vector float *)(ptrC+3*M+OFFSET+16));		\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B); SPU_LNOP;								\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); c0_1C = *((volatile vector float *)(ptrC+OFFSET+20));			\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); c1_1C = *((volatile vector float *)(ptrC+M+OFFSET+20));		\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); c2_1C = *((volatile vector float *)(ptrC+2*M+OFFSET+20));		\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); c3_1C = *((volatile vector float *)(ptrC+3*M+OFFSET+20));		\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); b0_0C = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+16));		\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); b1_0C = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); b2_0C = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); b3_0C = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); SPU_LNOP;								\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); b0_1C = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+20));		\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); b1_1C = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); b2_1C = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); b3_1C = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); *((volatile vector float *)(ptrC+OFFSET)) = c0_0A;			\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0A;			\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0A;		\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0A;		\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); SPU_LNOP;								\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1A;			\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1A;		\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1A;		\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1A;		\
}

#define StageACB(OFFSET,OFFB)										\
{													\
  SPU_FMA(c0_0C,a00,b0_0C,c0_0C); c0_0A = *((volatile vector float *)(ptrC+OFFSET+16));			\
  SPU_FMA(c1_0C,a10,b0_0C,c1_0C); c1_0A = *((volatile vector float *)(ptrC+M+OFFSET+16));		\
  SPU_FMA(c2_0C,a20,b0_0C,c2_0C); c2_0A = *((volatile vector float *)(ptrC+2*M+OFFSET+16));		\
  SPU_FMA(c3_0C,a30,b0_0C,c3_0C); SPU_LNOP;								\
  SPU_FMA(c0_1C,a00,b0_1C,c0_1C); c3_0A = *((volatile vector float *)(ptrC+3*M+OFFSET+16));		\
  SPU_FMA(c1_1C,a10,b0_1C,c1_1C); c0_1A = *((volatile vector float *)(ptrC+OFFSET+20));			\
  SPU_FMA(c2_1C,a20,b0_1C,c2_1C); c1_1A = *((volatile vector float *)(ptrC+M+OFFSET+20));		\
  SPU_FMA(c3_1C,a30,b0_1C,c3_1C); SPU_LNOP;								\
  SPU_FMA(c0_0C,a01,b1_0C,c0_0C); c2_1A = *((volatile vector float *)(ptrC+2*M+OFFSET+20));		\
  SPU_FMA(c1_0C,a11,b1_0C,c1_0C); c3_1A = *((volatile vector float *)(ptrC+3*M+OFFSET+20));		\
  SPU_FMA(c2_0C,a21,b1_0C,c2_0C); b0_0A = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+16));		\
  SPU_FMA(c3_0C,a31,b1_0C,c3_0C); SPU_LNOP;								\
  SPU_FMA(c0_1C,a01,b1_1C,c0_1C); b1_0A = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c1_1C,a11,b1_1C,c1_1C); b2_0A = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c2_1C,a21,b1_1C,c2_1C); b3_0A = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c3_1C,a31,b1_1C,c3_1C); SPU_LNOP;								\
  SPU_FMA(c0_0C,a02,b2_0C,c0_0C); b0_1A = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+20));		\
  SPU_FMA(c1_0C,a12,b2_0C,c1_0C); b1_1A = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c2_0C,a22,b2_0C,c2_0C); b2_1A = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c3_0C,a32,b2_0C,c3_0C); SPU_LNOP;								\
  SPU_FMA(c0_1C,a02,b2_1C,c0_1C); b3_1A = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c1_1C,a12,b2_1C,c1_1C); *((volatile vector float *)(ptrC+OFFSET)) = c0_0B;			\
  SPU_FMA(c2_1C,a22,b2_1C,c2_1C); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0B;			\
  SPU_FMA(c3_1C,a32,b2_1C,c3_1C); SPU_LNOP;								\
  SPU_FMA(c0_0C,a03,b3_0C,c0_0C); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0B;		\
  SPU_FMA(c1_0C,a13,b3_0C,c1_0C); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0B;		\
  SPU_FMA(c2_0C,a23,b3_0C,c2_0C); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1B;			\
  SPU_FMA(c3_0C,a33,b3_0C,c3_0C); SPU_LNOP;								\
  SPU_FMA(c0_1C,a03,b3_1C,c0_1C); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1B;		\
  SPU_FMA(c1_1C,a13,b3_1C,c1_1C); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1B;		\
  SPU_FMA(c2_1C,a23,b3_1C,c2_1C); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1B;		\
  SPU_FMA(c3_1C,a33,b3_1C,c3_1C); SPU_LNOP;								\
}

#define StageBAC(OFFSET,OFFB)										\
{													\
  SPU_FMA(c0_0A,a00,b0_0A,c0_0A); c0_0B = *((volatile vector float *)(ptrC+OFFSET+16));			\
  SPU_FMA(c1_0A,a10,b0_0A,c1_0A); c1_0B = *((volatile vector float *)(ptrC+M+OFFSET+16));		\
  SPU_FMA(c2_0A,a20,b0_0A,c2_0A); c2_0B = *((volatile vector float *)(ptrC+2*M+OFFSET+16));		\
  SPU_FMA(c3_0A,a30,b0_0A,c3_0A); SPU_LNOP;								\
  SPU_FMA(c0_1A,a00,b0_1A,c0_1A); c3_0B = *((volatile vector float *)(ptrC+3*M+OFFSET+16));		\
  SPU_FMA(c1_1A,a10,b0_1A,c1_1A); c0_1B = *((volatile vector float *)(ptrC+OFFSET+20));			\
  SPU_FMA(c2_1A,a20,b0_1A,c2_1A); c1_1B = *((volatile vector float *)(ptrC+M+OFFSET+20));		\
  SPU_FMA(c3_1A,a30,b0_1A,c3_1A); SPU_LNOP;								\
  SPU_FMA(c0_0A,a01,b1_0A,c0_0A); c2_1B = *((volatile vector float *)(ptrC+2*M+OFFSET+20));		\
  SPU_FMA(c1_0A,a11,b1_0A,c1_0A); c3_1B = *((volatile vector float *)(ptrC+3*M+OFFSET+20));		\
  SPU_FMA(c2_0A,a21,b1_0A,c2_0A); b0_0B = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+16));		\
  SPU_FMA(c3_0A,a31,b1_0A,c3_0A); SPU_LNOP;								\
  SPU_FMA(c0_1A,a01,b1_1A,c0_1A); b1_0B = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c1_1A,a11,b1_1A,c1_1A); b2_0B = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c2_1A,a21,b1_1A,c2_1A); b3_0B = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+16));	\
  SPU_FMA(c3_1A,a31,b1_1A,c3_1A); SPU_LNOP;								\
  SPU_FMA(c0_0A,a02,b2_0A,c0_0A); b0_1B = *((volatile vector float *)(ptrB+OFFSET+OFFB*M+20));		\
  SPU_FMA(c1_0A,a12,b2_0A,c1_0A); b1_1B = *((volatile vector float *)(ptrB+M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c2_0A,a22,b2_0A,c2_0A); b2_1B = *((volatile vector float *)(ptrB+2*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c3_0A,a32,b2_0A,c3_0A); SPU_LNOP;								\
  SPU_FMA(c0_1A,a02,b2_1A,c0_1A); b3_1B = *((volatile vector float *)(ptrB+3*M+OFFSET+OFFB*M+20));	\
  SPU_FMA(c1_1A,a12,b2_1A,c1_1A); *((volatile vector float *)(ptrC+OFFSET)) = c0_0C;			\
  SPU_FMA(c2_1A,a22,b2_1A,c2_1A); *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0C;			\
  SPU_FMA(c3_1A,a32,b2_1A,c3_1A); SPU_LNOP;								\
  SPU_FMA(c0_0A,a03,b3_0A,c0_0A); *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0C;		\
  SPU_FMA(c1_0A,a13,b3_0A,c1_0A); *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0C;		\
  SPU_FMA(c2_0A,a23,b3_0A,c2_0A); *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1C;			\
  SPU_FMA(c3_0A,a33,b3_0A,c3_0A); SPU_LNOP;								\
  SPU_FMA(c0_1A,a03,b3_1A,c0_1A); *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1C;		\
  SPU_FMA(c1_1A,a13,b3_1A,c1_1A); *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1C;		\
  SPU_FMA(c2_1A,a23,b3_1A,c2_1A); *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1C;		\
  SPU_FMA(c3_1A,a33,b3_1A,c3_1A); SPU_LNOP;								\
}

#define StageMISC(OFFA,OFFB)									\
{												\
  SPU_FMA(c0_0B,a00,b0_0B,c0_0B); a0 = *((volatile vector float *)(ptrA+OFFA+4));		\
  SPU_FMA(c1_0B,a10,b0_0B,c1_0B); a1 = *((volatile vector float *)(ptrA+M+OFFA+4));		\
  SPU_FMA(c2_0B,a20,b0_0B,c2_0B); a2 = *((volatile vector float *)(ptrA+2*M+OFFA+4));		\
  SPU_FMA(c3_0B,a30,b0_0B,c3_0B); a3 = *((volatile vector float *)(ptrA+3*M+OFFA+4));		\
  SPU_FMA(c0_1B,a00,b0_1B,c0_1B); *((volatile vector float *)(ptrC+48)) = c0_0A;		\
  SPU_FMA(c1_1B,a10,b0_1B,c1_1B); *((volatile vector float *)(ptrC+M+48)) = c1_0A;		\
  SPU_FMA(c2_1B,a20,b0_1B,c2_1B); a00 = spu_shuffle(a0, a0, pat0);				\
  SPU_FMA(c3_1B,a30,b0_1B,c3_1B); *((volatile vector float *)(ptrC+2*M+48)) = c2_0A;		\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B); *((volatile vector float *)(ptrC+3*M+48)) = c3_0A;		\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B); a10 = spu_shuffle(a1, a1, pat0);				\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); *((volatile vector float *)(ptrC+52)) = c0_1A;		\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); *((volatile vector float *)(ptrC+M+52)) = c1_1A;		\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); a20 = spu_shuffle(a2, a2, pat0);				\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); *((volatile vector float *)(ptrC+2*M+52)) = c2_1A;		\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); *((volatile vector float *)(ptrC+3*M+52)) = c3_1A;		\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); a30 = spu_shuffle(a3, a3, pat0);				\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); c0_0A = *((volatile vector float *)(ptrC));			\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); c1_0A = *((volatile vector float *)(ptrC+M));			\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); a01 = spu_shuffle(a0, a0, pat1);				\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); c2_0A = *((volatile vector float *)(ptrC+2*M));		\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); c3_0A = *((volatile vector float *)(ptrC+3*M));		\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); a11 = spu_shuffle(a1, a1, pat1);				\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); b0_0A = *((volatile vector float *)(ptrB+4*M+OFFB*M));	\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); b0_1A = *((volatile vector float *)(ptrB+4*M+OFFB*M+4));	\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); a21 = spu_shuffle(a2, a2, pat1);				\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); b1_0A = *((volatile vector float *)(ptrB+5*M+OFFB*M));	\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); b1_1A = *((volatile vector float *)(ptrB+5*M+OFFB*M+4));	\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); a31 = spu_shuffle(a3, a3, pat1);				\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); c0_1A = *((volatile vector float *)(ptrC+4));			\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); c1_1A = *((volatile vector float *)(ptrC+M+4));		\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); a02 = spu_shuffle(a0, a0, pat2);				\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); c2_1A = *((volatile vector float *)(ptrC+2*M+4));		\
  SPU_FMA(c0_0A,a00,b0_0A,c0_0A); c3_1A = *((volatile vector float *)(ptrC+3*M+4));		\
  SPU_FMA(c1_0A,a10,b0_0A,c1_0A); a12 = spu_shuffle(a1, a1, pat2);				\
  SPU_FMA(c2_0A,a20,b0_0A,c2_0A); b2_0A = *((volatile vector float *)(ptrB+6*M+OFFB*M));	\
  SPU_FMA(c3_0A,a30,b0_0A,c3_0A); b2_1A = *((volatile vector float *)(ptrB+6*M+OFFB*M+4));	\
  SPU_FMA(c0_1A,a00,b0_1A,c0_1A); a22 = spu_shuffle(a2, a2, pat2);				\
  SPU_FMA(c1_1A,a10,b0_1A,c1_1A); b3_0A = *((volatile vector float *)(ptrB+7*M+OFFB*M));	\
  SPU_FMA(c2_1A,a20,b0_1A,c2_1A); b3_1A = *((volatile vector float *)(ptrB+7*M+OFFB*M+4));	\
  SPU_FMA(c3_1A,a30,b0_1A,c3_1A); a32 = spu_shuffle(a3, a3, pat2);				\
  SPU_FMA(c0_0A,a01,b1_0A,c0_0A); *((volatile vector float *)(ptrC+56)) = c0_0B;		\
  SPU_FMA(c1_0A,a11,b1_0A,c1_0A); *((volatile vector float *)(ptrC+M+56)) = c1_0B;		\
  SPU_FMA(c2_0A,a21,b1_0A,c2_0A); a03 = spu_shuffle(a0, a0, pat3);				\
  SPU_FMA(c3_0A,a31,b1_0A,c3_0A); *((volatile vector float *)(ptrC+2*M+56)) = c2_0B;		\
  SPU_FMA(c0_1A,a01,b1_1A,c0_1A); *((volatile vector float *)(ptrC+3*M+56)) = c3_0B;		\
  SPU_FMA(c1_1A,a11,b1_1A,c1_1A); a13 = spu_shuffle(a1, a1, pat3);				\
  SPU_FMA(c2_1A,a21,b1_1A,c2_1A); *((volatile vector float *)(ptrC+60)) = c0_1B;		\
  SPU_FMA(c3_1A,a31,b1_1A,c3_1A); *((volatile vector float *)(ptrC+M+60)) = c1_1B;		\
  SPU_FMA(c0_0A,a02,b2_0A,c0_0A); a23 = spu_shuffle(a2, a2, pat3);				\
  SPU_FMA(c1_0A,a12,b2_0A,c1_0A); *((volatile vector float *)(ptrC+2*M+60)) = c2_1B;		\
  SPU_FMA(c2_0A,a22,b2_0A,c2_0A); *((volatile vector float *)(ptrC+3*M+60)) = c3_1B;		\
  SPU_FMA(c3_0A,a32,b2_0A,c3_0A); a33 = spu_shuffle(a3, a3, pat3);				\
  SPU_FMA(c0_1A,a02,b2_1A,c0_1A); b0_0B = *((volatile vector float *)(ptrB+4*M+OFFB*M+8));	\
  SPU_FMA(c1_1A,a12,b2_1A,c1_1A); b0_1B = *((volatile vector float *)(ptrB+4*M+OFFB*M+12));	\
  SPU_FMA(c2_1A,a22,b2_1A,c2_1A); b1_0B = *((volatile vector float *)(ptrB+5*M+OFFB*M+8));	\
  SPU_FMA(c3_1A,a32,b2_1A,c3_1A); b1_1B = *((volatile vector float *)(ptrB+5*M+OFFB*M+12));	\
  SPU_FMA(c0_0A,a03,b3_0A,c0_0A); c0_0B = *((volatile vector float *)(ptrC+8));			\
  SPU_FMA(c1_0A,a13,b3_0A,c1_0A); c1_0B = *((volatile vector float *)(ptrC+M+8));		\
  SPU_FMA(c2_0A,a23,b3_0A,c2_0A); c2_0B = *((volatile vector float *)(ptrC+2*M+8));		\
  SPU_FMA(c3_0A,a33,b3_0A,c3_0A); c3_0B = *((volatile vector float *)(ptrC+3*M+8));		\
  SPU_FMA(c0_1A,a03,b3_1A,c0_1A); c0_1B = *((volatile vector float *)(ptrC+12));		\
  SPU_FMA(c1_1A,a13,b3_1A,c1_1A); c1_1B = *((volatile vector float *)(ptrC+M+12));		\
  SPU_FMA(c2_1A,a23,b3_1A,c2_1A); c2_1B = *((volatile vector float *)(ptrC+2*M+12));		\
  SPU_FMA(c3_1A,a33,b3_1A,c3_1A); c3_1B = *((volatile vector float *)(ptrC+3*M+12));		\
}

#define StageMISCmod(OFFA,OFFB)									\
{												\
  SPU_FMA(c0_0B,a00,b0_0B,c0_0B); a0 = *((volatile vector float *)(ptrA+OFFA+4));		\
  SPU_FMA(c1_0B,a10,b0_0B,c1_0B); a1 = *((volatile vector float *)(ptrA+M+OFFA+4));		\
  SPU_FMA(c2_0B,a20,b0_0B,c2_0B); a2 = *((volatile vector float *)(ptrA+2*M+OFFA+4));		\
  SPU_FMA(c3_0B,a30,b0_0B,c3_0B); a3 = *((volatile vector float *)(ptrA+3*M+OFFA+4));		\
  SPU_FMA(c0_1B,a00,b0_1B,c0_1B); *((volatile vector float *)(ptrC+48)) = c0_0A;		\
  SPU_FMA(c1_1B,a10,b0_1B,c1_1B); *((volatile vector float *)(ptrC+M+48)) = c1_0A;		\
  SPU_FMA(c2_1B,a20,b0_1B,c2_1B); a00 = spu_shuffle(a0, a0, pat0);				\
  SPU_FMA(c3_1B,a30,b0_1B,c3_1B); *((volatile vector float *)(ptrC+2*M+48)) = c2_0A;		\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B); *((volatile vector float *)(ptrC+3*M+48)) = c3_0A;		\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B); a10 = spu_shuffle(a1, a1, pat0);				\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); *((volatile vector float *)(ptrC+52)) = c0_1A;		\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); *((volatile vector float *)(ptrC+M+52)) = c1_1A;		\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); a20 = spu_shuffle(a2, a2, pat0);				\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); *((volatile vector float *)(ptrC+2*M+52)) = c2_1A;		\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); *((volatile vector float *)(ptrC+3*M+52)) = c3_1A;		\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); a30 = spu_shuffle(a3, a3, pat0);				\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); c0_0A = *((volatile vector float *)(ptrC));			\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); c1_0A = *((volatile vector float *)(ptrC+M));			\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); a01 = spu_shuffle(a0, a0, pat1);				\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); c2_0A = *((volatile vector float *)(ptrC+2*M));		\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); c3_0A = *((volatile vector float *)(ptrC+3*M));		\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); a11 = spu_shuffle(a1, a1, pat1);				\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); b0_0A = *((volatile vector float *)(ptrB+4*M+OFFB*M));	\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); b0_1A = *((volatile vector float *)(ptrB+4*M+OFFB*M+4));	\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); a21 = spu_shuffle(a2, a2, pat1);				\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); b1_0A = *((volatile vector float *)(ptrB+5*M+OFFB*M));	\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); b1_1A = *((volatile vector float *)(ptrB+5*M+OFFB*M+4));	\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); a31 = spu_shuffle(a3, a3, pat1);				\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); c0_1A = *((volatile vector float *)(ptrC+4));			\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); c1_1A = *((volatile vector float *)(ptrC+M+4));		\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); a02 = spu_shuffle(a0, a0, pat2);				\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); c2_1A = *((volatile vector float *)(ptrC+2*M+4));		\
  SPU_FMA(c0_0A,a00,b0_0A,c0_0A); c3_1A = *((volatile vector float *)(ptrC+3*M+4));		\
  SPU_FMA(c1_0A,a10,b0_0A,c1_0A); a12 = spu_shuffle(a1, a1, pat2);				\
  SPU_FMA(c2_0A,a20,b0_0A,c2_0A); b2_0A = *((volatile vector float *)(ptrB+6*M+OFFB*M));	\
  SPU_FMA(c3_0A,a30,b0_0A,c3_0A); b2_1A = *((volatile vector float *)(ptrB+6*M+OFFB*M+4));	\
  SPU_FMA(c0_1A,a00,b0_1A,c0_1A); a22 = spu_shuffle(a2, a2, pat2);				\
  SPU_FMA(c1_1A,a10,b0_1A,c1_1A); b3_0A = *((volatile vector float *)(ptrB+7*M+OFFB*M));	\
  SPU_FMA(c2_1A,a20,b0_1A,c2_1A); b3_1A = *((volatile vector float *)(ptrB+7*M+OFFB*M+4));	\
  SPU_FMA(c3_1A,a30,b0_1A,c3_1A); a32 = spu_shuffle(a3, a3, pat2);				\
  SPU_FMA(c0_0A,a01,b1_0A,c0_0A); *((volatile vector float *)(ptrC+56)) = c0_0B;		\
  SPU_FMA(c1_0A,a11,b1_0A,c1_0A); *((volatile vector float *)(ptrC+M+56)) = c1_0B;		\
  SPU_FMA(c2_0A,a21,b1_0A,c2_0A); a03 = spu_shuffle(a0, a0, pat3);				\
  SPU_FMA(c3_0A,a31,b1_0A,c3_0A); *((volatile vector float *)(ptrC+2*M+56)) = c2_0B;		\
  SPU_FMA(c0_1A,a01,b1_1A,c0_1A); *((volatile vector float *)(ptrC+3*M+56)) = c3_0B;		\
  SPU_FMA(c1_1A,a11,b1_1A,c1_1A); a13 = spu_shuffle(a1, a1, pat3);				\
  SPU_FMA(c2_1A,a21,b1_1A,c2_1A); *((volatile vector float *)(ptrC+60)) = c0_1B;		\
  SPU_FMA(c3_1A,a31,b1_1A,c3_1A); *((volatile vector float *)(ptrC+M+60)) = c1_1B;		\
  SPU_FMA(c0_0A,a02,b2_0A,c0_0A); a23 = spu_shuffle(a2, a2, pat3);				\
  SPU_FMA(c1_0A,a12,b2_0A,c1_0A); *((volatile vector float *)(ptrC+2*M+60)) = c2_1B;		\
  SPU_FMA(c2_0A,a22,b2_0A,c2_0A); *((volatile vector float *)(ptrC+3*M+60)) = c3_1B;		\
  SPU_FMA(c3_0A,a32,b2_0A,c3_0A); a33 = spu_shuffle(a3, a3, pat3);				\
  SPU_FMA(c0_1A,a02,b2_1A,c0_1A); b0_0B = *((volatile vector float *)(ptrB+4*M+OFFB*M+8));	\
  SPU_FMA(c1_1A,a12,b2_1A,c1_1A); b0_1B = *((volatile vector float *)(ptrB+4*M+OFFB*M+12));	\
  SPU_FMA(c2_1A,a22,b2_1A,c2_1A); b1_0B = *((volatile vector float *)(ptrB+5*M+OFFB*M+8));	\
  SPU_FMA(c3_1A,a32,b2_1A,c3_1A); b1_1B = *((volatile vector float *)(ptrB+5*M+OFFB*M+12));	\
  SPU_FMA(c0_0A,a03,b3_0A,c0_0A); c0_0B = *((volatile vector float *)(ptrC+8));			\
  SPU_FMA(c1_0A,a13,b3_0A,c1_0A); c1_0B = *((volatile vector float *)(ptrC+M+8));		\
  SPU_FMA(c2_0A,a23,b3_0A,c2_0A); c2_0B = *((volatile vector float *)(ptrC+2*M+8));		\
  SPU_FMA(c3_0A,a33,b3_0A,c3_0A); c3_0B = *((volatile vector float *)(ptrC+3*M+8));		\
  SPU_FMA(c0_1A,a03,b3_1A,c0_1A); b2_0B = *((volatile vector float *)(ptrB+6*M+OFFB*M+8));	\
  SPU_FMA(c1_1A,a13,b3_1A,c1_1A); b2_1B = *((volatile vector float *)(ptrB+6*M+OFFB*M+12));	\
  SPU_FMA(c2_1A,a23,b3_1A,c2_1A); b3_0B = *((volatile vector float *)(ptrB+7*M+OFFB*M+8));	\
  SPU_FMA(c3_1A,a33,b3_1A,c3_1A); b3_1B = *((volatile vector float *)(ptrB+7*M+OFFB*M+12));	\
  ALIGN8B;											\
  c0_1B = *((volatile vector float *)(ptrC+12));						\
  c1_1B = *((volatile vector float *)(ptrC+M+12));						\
  c2_1B = *((volatile vector float *)(ptrC+2*M+12));						\
  c3_1B = *((volatile vector float *)(ptrC+3*M+12));						\
  ptrB += OFFB*M;										\
  ALIGN8B;											\
}

#define StageMISCclr()									\
{											\
  SPU_FM(c0_0B,a00,b0_0B);        a0 = *((volatile vector float *)(ptrA+4));		\
  SPU_FM(c1_0B,a10,b0_0B);        a1 = *((volatile vector float *)(ptrA+M+4));		\
  SPU_FM(c2_0B,a20,b0_0B);        a2 = *((volatile vector float *)(ptrA+2*M+4));	\
  SPU_FM(c3_0B,a30,b0_0B);        a3 = *((volatile vector float *)(ptrA+3*M+4));	\
  SPU_FM(c0_1B,a00,b0_1B);        *((volatile vector float *)(ptrC+48)) = c0_0A;	\
  SPU_FM(c1_1B,a10,b0_1B);        *((volatile vector float *)(ptrC+M+48)) = c1_0A;	\
  SPU_FM(c2_1B,a20,b0_1B);        a00 = spu_shuffle(a0, a0, pat0);			\
  SPU_FM(c3_1B,a30,b0_1B);        *((volatile vector float *)(ptrC+2*M+48)) = c2_0A;	\
  SPU_FMA(c0_0B,a01,b1_0B,c0_0B); *((volatile vector float *)(ptrC+3*M+48)) = c3_0A;	\
  SPU_FMA(c1_0B,a11,b1_0B,c1_0B); a10 = spu_shuffle(a1, a1, pat0);			\
  SPU_FMA(c2_0B,a21,b1_0B,c2_0B); *((volatile vector float *)(ptrC+52)) = c0_1A;	\
  SPU_FMA(c3_0B,a31,b1_0B,c3_0B); *((volatile vector float *)(ptrC+M+52)) = c1_1A;	\
  SPU_FMA(c0_1B,a01,b1_1B,c0_1B); a20 = spu_shuffle(a2, a2, pat0);			\
  SPU_FMA(c1_1B,a11,b1_1B,c1_1B); *((volatile vector float *)(ptrC+2*M+52)) = c2_1A;	\
  SPU_FMA(c2_1B,a21,b1_1B,c2_1B); *((volatile vector float *)(ptrC+3*M+52)) = c3_1A;	\
  SPU_FMA(c3_1B,a31,b1_1B,c3_1B); a30 = spu_shuffle(a3, a3, pat0);			\
  SPU_FMA(c0_0B,a02,b2_0B,c0_0B); c0_0A = *((volatile vector float *)(ptrC));		\
  SPU_FMA(c1_0B,a12,b2_0B,c1_0B); c1_0A = *((volatile vector float *)(ptrC+M));		\
  SPU_FMA(c2_0B,a22,b2_0B,c2_0B); a01 = spu_shuffle(a0, a0, pat1);			\
  SPU_FMA(c3_0B,a32,b2_0B,c3_0B); c2_0A = *((volatile vector float *)(ptrC+2*M));	\
  SPU_FMA(c0_1B,a02,b2_1B,c0_1B); c3_0A = *((volatile vector float *)(ptrC+3*M));	\
  SPU_FMA(c1_1B,a12,b2_1B,c1_1B); a11 = spu_shuffle(a1, a1, pat1);			\
  SPU_FMA(c2_1B,a22,b2_1B,c2_1B); b0_0A = *((volatile vector float *)(ptrB+4*M));	\
  SPU_FMA(c3_1B,a32,b2_1B,c3_1B); b0_1A = *((volatile vector float *)(ptrB+4*M+4));	\
  SPU_FMA(c0_0B,a03,b3_0B,c0_0B); a21 = spu_shuffle(a2, a2, pat1);			\
  SPU_FMA(c1_0B,a13,b3_0B,c1_0B); b1_0A = *((volatile vector float *)(ptrB+5*M));	\
  SPU_FMA(c2_0B,a23,b3_0B,c2_0B); b1_1A = *((volatile vector float *)(ptrB+5*M+4));	\
  SPU_FMA(c3_0B,a33,b3_0B,c3_0B); a31 = spu_shuffle(a3, a3, pat1);			\
  SPU_FMA(c0_1B,a03,b3_1B,c0_1B); c0_1A = *((volatile vector float *)(ptrC+4));		\
  SPU_FMA(c1_1B,a13,b3_1B,c1_1B); c1_1A = *((volatile vector float *)(ptrC+M+4));	\
  SPU_FMA(c2_1B,a23,b3_1B,c2_1B); a02 = spu_shuffle(a0, a0, pat2);			\
  SPU_FMA(c3_1B,a33,b3_1B,c3_1B); c2_1A = *((volatile vector float *)(ptrC+2*M+4));	\
  SPU_FMA(c0_0A,a00,b0_0A,c0_0A); c3_1A = *((volatile vector float *)(ptrC+3*M+4));	\
  SPU_FMA(c1_0A,a10,b0_0A,c1_0A); a12 = spu_shuffle(a1, a1, pat2);			\
  SPU_FMA(c2_0A,a20,b0_0A,c2_0A); b2_0A = *((volatile vector float *)(ptrB+6*M));	\
  SPU_FMA(c3_0A,a30,b0_0A,c3_0A); b2_1A = *((volatile vector float *)(ptrB+6*M+4));	\
  SPU_FMA(c0_1A,a00,b0_1A,c0_1A); a22 = spu_shuffle(a2, a2, pat2);			\
  SPU_FMA(c1_1A,a10,b0_1A,c1_1A); b3_0A = *((volatile vector float *)(ptrB+7*M));	\
  SPU_FMA(c2_1A,a20,b0_1A,c2_1A); b3_1A = *((volatile vector float *)(ptrB+7*M+4));	\
  SPU_FMA(c3_1A,a30,b0_1A,c3_1A); a32 = spu_shuffle(a3, a3, pat2);			\
  SPU_FMA(c0_0A,a01,b1_0A,c0_0A); *((volatile vector float *)(ptrC+56)) = c0_0B;	\
  SPU_FMA(c1_0A,a11,b1_0A,c1_0A); *((volatile vector float *)(ptrC+M+56)) = c1_0B;	\
  SPU_FMA(c2_0A,a21,b1_0A,c2_0A); a03 = spu_shuffle(a0, a0, pat3);			\
  SPU_FMA(c3_0A,a31,b1_0A,c3_0A); *((volatile vector float *)(ptrC+2*M+56)) = c2_0B;	\
  SPU_FMA(c0_1A,a01,b1_1A,c0_1A); *((volatile vector float *)(ptrC+3*M+56)) = c3_0B;	\
  SPU_FMA(c1_1A,a11,b1_1A,c1_1A); a13 = spu_shuffle(a1, a1, pat3);			\
  SPU_FMA(c2_1A,a21,b1_1A,c2_1A); *((volatile vector float *)(ptrC+60)) = c0_1B;	\
  SPU_FMA(c3_1A,a31,b1_1A,c3_1A); *((volatile vector float *)(ptrC+M+60)) = c1_1B;	\
  SPU_FMA(c0_0A,a02,b2_0A,c0_0A); a23 = spu_shuffle(a2, a2, pat3);			\
  SPU_FMA(c1_0A,a12,b2_0A,c1_0A); *((volatile vector float *)(ptrC+2*M+60)) = c2_1B;	\
  SPU_FMA(c2_0A,a22,b2_0A,c2_0A); *((volatile vector float *)(ptrC+3*M+60)) = c3_1B;	\
  SPU_FMA(c3_0A,a32,b2_0A,c3_0A); a33 = spu_shuffle(a3, a3, pat3);			\
  SPU_FMA(c0_1A,a02,b2_1A,c0_1A); b0_0B = *((volatile vector float *)(ptrB+4*M+8));	\
  SPU_FMA(c1_1A,a12,b2_1A,c1_1A); b0_1B = *((volatile vector float *)(ptrB+4*M+12));	\
  SPU_FMA(c2_1A,a22,b2_1A,c2_1A); b1_0B = *((volatile vector float *)(ptrB+5*M+8));	\
  SPU_FMA(c3_1A,a32,b2_1A,c3_1A); b1_1B = *((volatile vector float *)(ptrB+5*M+12));	\
  SPU_FMA(c0_0A,a03,b3_0A,c0_0A); c0_0B = *((volatile vector float *)(ptrC+8));		\
  SPU_FMA(c1_0A,a13,b3_0A,c1_0A); c1_0B = *((volatile vector float *)(ptrC+M+8));	\
  SPU_FMA(c2_0A,a23,b3_0A,c2_0A); c2_0B = *((volatile vector float *)(ptrC+2*M+8));	\
  SPU_FMA(c3_0A,a33,b3_0A,c3_0A); c3_0B = *((volatile vector float *)(ptrC+3*M+8));	\
  SPU_FMA(c0_1A,a03,b3_1A,c0_1A); b2_0B = *((volatile vector float *)(ptrB+6*M+8));	\
  SPU_FMA(c1_1A,a13,b3_1A,c1_1A); b2_1B = *((volatile vector float *)(ptrB+6*M+12));	\
  SPU_FMA(c2_1A,a23,b3_1A,c2_1A); b3_0B = *((volatile vector float *)(ptrB+7*M+8));	\
  SPU_FMA(c3_1A,a33,b3_1A,c3_1A); b3_1B = *((volatile vector float *)(ptrB+7*M+12));	\
  ALIGN8B;										\
  c0_1B = *((volatile vector float *)(ptrC+12));					\
  c1_1B = *((volatile vector float *)(ptrC+M+12));					\
  c2_1B = *((volatile vector float *)(ptrC+2*M+12));					\
  c3_1B = *((volatile vector float *)(ptrC+3*M+12));					\
}

#define Loads4RegSetA(OFFSET)				\
{							\
  c0_0A = *((volatile vector float *)(ptrC+OFFSET));		\
  c1_0A = *((volatile vector float *)(ptrC+M+OFFSET));		\
  c2_0A = *((volatile vector float *)(ptrC+2*M+OFFSET));	\
  c3_0A = *((volatile vector float *)(ptrC+3*M+OFFSET));	\
  c0_1A = *((volatile vector float *)(ptrC+OFFSET+4));		\
  c1_1A = *((volatile vector float *)(ptrC+M+OFFSET+4));	\
  c2_1A = *((volatile vector float *)(ptrC+2*M+OFFSET+4));	\
  c3_1A = *((volatile vector float *)(ptrC+3*M+OFFSET+4));	\
  b0_0A = *((volatile vector float *)(ptrB+OFFSET));		\
  b1_0A = *((volatile vector float *)(ptrB+M+OFFSET));		\
  b2_0A = *((volatile vector float *)(ptrB+2*M+OFFSET));	\
  b3_0A = *((volatile vector float *)(ptrB+3*M+OFFSET));	\
  b0_1A = *((volatile vector float *)(ptrB+OFFSET+4));		\
  b1_1A = *((volatile vector float *)(ptrB+M+OFFSET+4));	\
  b2_1A = *((volatile vector float *)(ptrB+2*M+OFFSET+4));	\
  b3_1A = *((volatile vector float *)(ptrB+3*M+OFFSET+4));	\
}

#define Loads4RegSetAClr(OFFSET)			\
{							\
  b0_0A = *((volatile vector float *)(ptrB+OFFSET));		\
  b1_0A = *((volatile vector float *)(ptrB+M+OFFSET));		\
  b2_0A = *((volatile vector float *)(ptrB+2*M+OFFSET));	\
  b3_0A = *((volatile vector float *)(ptrB+3*M+OFFSET));	\
  b0_1A = *((volatile vector float *)(ptrB+OFFSET+4));		\
  b1_1A = *((volatile vector float *)(ptrB+M+OFFSET+4));	\
  b2_1A = *((volatile vector float *)(ptrB+2*M+OFFSET+4));	\
  b3_1A = *((volatile vector float *)(ptrB+3*M+OFFSET+4));	\
}

#define Ops4RegSetAClr()			\
{						\
  c0_0A = spu_mul( a00, b0_0A);			\
  c1_0A = spu_mul( a10, b0_0A);			\
  c2_0A = spu_mul( a20, b0_0A);			\
  c3_0A = spu_mul( a30, b0_0A);			\
  c0_1A = spu_mul( a00, b0_1A);			\
  c1_1A = spu_mul( a10, b0_1A);			\
  c2_1A = spu_mul( a20, b0_1A);			\
  c3_1A = spu_mul( a30, b0_1A);			\
  c0_0A = spu_madd(a01, b1_0A, c0_0A);		\
  c1_0A = spu_madd(a11, b1_0A, c1_0A);		\
  c2_0A = spu_madd(a21, b1_0A, c2_0A);		\
  c3_0A = spu_madd(a31, b1_0A, c3_0A);		\
  c0_1A = spu_madd(a01, b1_1A, c0_1A);		\
  c1_1A = spu_madd(a11, b1_1A, c1_1A);		\
  c2_1A = spu_madd(a21, b1_1A, c2_1A);		\
  c3_1A = spu_madd(a31, b1_1A, c3_1A);		\
  c0_0A = spu_madd(a02, b2_0A, c0_0A);		\
  c1_0A = spu_madd(a12, b2_0A, c1_0A);		\
  c2_0A = spu_madd(a22, b2_0A, c2_0A);		\
  c3_0A = spu_madd(a32, b2_0A, c3_0A);		\
  c0_1A = spu_madd(a02, b2_1A, c0_1A);		\
  c1_1A = spu_madd(a12, b2_1A, c1_1A);		\
  c2_1A = spu_madd(a22, b2_1A, c2_1A);		\
  c3_1A = spu_madd(a32, b2_1A, c3_1A);		\
  c0_0A = spu_madd(a03, b3_0A, c0_0A);		\
  c1_0A = spu_madd(a13, b3_0A, c1_0A);		\
  c2_0A = spu_madd(a23, b3_0A, c2_0A);		\
  c3_0A = spu_madd(a33, b3_0A, c3_0A);		\
  c0_1A = spu_madd(a03, b3_1A, c0_1A);		\
  c1_1A = spu_madd(a13, b3_1A, c1_1A);		\
  c2_1A = spu_madd(a23, b3_1A, c2_1A);		\
  c3_1A = spu_madd(a33, b3_1A, c3_1A);		\
}

#define Ops4RegSetA()				\
{						\
  c0_0A = spu_madd(a00, b0_0A, c0_0A);		\
  c1_0A = spu_madd(a10, b0_0A, c1_0A);		\
  c2_0A = spu_madd(a20, b0_0A, c2_0A);		\
  c3_0A = spu_madd(a30, b0_0A, c3_0A);		\
  c0_1A = spu_madd(a00, b0_1A, c0_1A);		\
  c1_1A = spu_madd(a10, b0_1A, c1_1A);		\
  c2_1A = spu_madd(a20, b0_1A, c2_1A);		\
  c3_1A = spu_madd(a30, b0_1A, c3_1A);		\
  c0_0A = spu_madd(a01, b1_0A, c0_0A);		\
  c1_0A = spu_madd(a11, b1_0A, c1_0A);		\
  c2_0A = spu_madd(a21, b1_0A, c2_0A);		\
  c3_0A = spu_madd(a31, b1_0A, c3_0A);		\
  c0_1A = spu_madd(a01, b1_1A, c0_1A);		\
  c1_1A = spu_madd(a11, b1_1A, c1_1A);		\
  c2_1A = spu_madd(a21, b1_1A, c2_1A);		\
  c3_1A = spu_madd(a31, b1_1A, c3_1A);		\
  c0_0A = spu_madd(a02, b2_0A, c0_0A);		\
  c1_0A = spu_madd(a12, b2_0A, c1_0A);		\
  c2_0A = spu_madd(a22, b2_0A, c2_0A);		\
  c3_0A = spu_madd(a32, b2_0A, c3_0A);		\
  c0_1A = spu_madd(a02, b2_1A, c0_1A);		\
  c1_1A = spu_madd(a12, b2_1A, c1_1A);		\
  c2_1A = spu_madd(a22, b2_1A, c2_1A);		\
  c3_1A = spu_madd(a32, b2_1A, c3_1A);		\
  c0_0A = spu_madd(a03, b3_0A, c0_0A);		\
  c1_0A = spu_madd(a13, b3_0A, c1_0A);		\
  c2_0A = spu_madd(a23, b3_0A, c2_0A);		\
  c3_0A = spu_madd(a33, b3_0A, c3_0A);		\
  c0_1A = spu_madd(a03, b3_1A, c0_1A);		\
  c1_1A = spu_madd(a13, b3_1A, c1_1A);		\
  c2_1A = spu_madd(a23, b3_1A, c2_1A);		\
  c3_1A = spu_madd(a33, b3_1A, c3_1A);		\
}

#define Stores4RegSetA(OFFSET)					\
{								\
  *((volatile vector float *)(ptrC+OFFSET)) = c0_0A;		\
  *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0A;		\
  *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0A;	\
  *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0A;	\
  *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1A;		\
  *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1A;	\
  *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1A;	\
  *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1A;	\
}

#define Loads4RegSetB(OFFSET)					\
{								\
  c0_0B = *((volatile vector float *)(ptrC+OFFSET));		\
  c1_0B = *((volatile vector float *)(ptrC+M+OFFSET));		\
  c2_0B = *((volatile vector float *)(ptrC+2*M+OFFSET));	\
  c3_0B = *((volatile vector float *)(ptrC+3*M+OFFSET));	\
  c0_1B = *((volatile vector float *)(ptrC+OFFSET+4));		\
  c1_1B = *((volatile vector float *)(ptrC+M+OFFSET+4));	\
  c2_1B = *((volatile vector float *)(ptrC+2*M+OFFSET+4));	\
  c3_1B = *((volatile vector float *)(ptrC+3*M+OFFSET+4));	\
  b0_0B = *((volatile vector float *)(ptrB+OFFSET));		\
  b1_0B = *((volatile vector float *)(ptrB+M+OFFSET));		\
  b2_0B = *((volatile vector float *)(ptrB+2*M+OFFSET));	\
  b3_0B = *((volatile vector float *)(ptrB+3*M+OFFSET));	\
  b0_1B = *((volatile vector float *)(ptrB+OFFSET+4));		\
  b1_1B = *((volatile vector float *)(ptrB+M+OFFSET+4));	\
  b2_1B = *((volatile vector float *)(ptrB+2*M+OFFSET+4));	\
  b3_1B = *((volatile vector float *)(ptrB+3*M+OFFSET+4));	\
}

#define Loads4RegSetBClr(OFFSET)				\
{								\
  b0_0B = *((volatile vector float *)(ptrB+OFFSET));		\
  b1_0B = *((volatile vector float *)(ptrB+M+OFFSET));		\
  b2_0B = *((volatile vector float *)(ptrB+2*M+OFFSET));	\
  b3_0B = *((volatile vector float *)(ptrB+3*M+OFFSET));	\
  b0_1B = *((volatile vector float *)(ptrB+OFFSET+4));		\
  b1_1B = *((volatile vector float *)(ptrB+M+OFFSET+4));	\
  b2_1B = *((volatile vector float *)(ptrB+2*M+OFFSET+4));	\
  b3_1B = *((volatile vector float *)(ptrB+3*M+OFFSET+4));	\
}

#define Ops4RegSetB()					\
{							\
  c0_0B = spu_madd(a00, b0_0B, c0_0B);			\
  c1_0B = spu_madd(a10, b0_0B, c1_0B);			\
  c2_0B = spu_madd(a20, b0_0B, c2_0B);			\
  c3_0B = spu_madd(a30, b0_0B, c3_0B);			\
  c0_1B = spu_madd(a00, b0_1B, c0_1B);			\
  c1_1B = spu_madd(a10, b0_1B, c1_1B);			\
  c2_1B = spu_madd(a20, b0_1B, c2_1B);			\
  c3_1B = spu_madd(a30, b0_1B, c3_1B);			\
  c0_0B = spu_madd(a01, b1_0B, c0_0B);			\
  c1_0B = spu_madd(a11, b1_0B, c1_0B);			\
  c2_0B = spu_madd(a21, b1_0B, c2_0B);			\
  c3_0B = spu_madd(a31, b1_0B, c3_0B);			\
  c0_1B = spu_madd(a01, b1_1B, c0_1B);			\
  c1_1B = spu_madd(a11, b1_1B, c1_1B);			\
  c2_1B = spu_madd(a21, b1_1B, c2_1B);			\
  c3_1B = spu_madd(a31, b1_1B, c3_1B);			\
  c0_0B = spu_madd(a02, b2_0B, c0_0B); SPU_LNOP;	\
  c1_0B = spu_madd(a12, b2_0B, c1_0B);			\
  c2_0B = spu_madd(a22, b2_0B, c2_0B);			\
  c3_0B = spu_madd(a32, b2_0B, c3_0B);			\
  c0_1B = spu_madd(a02, b2_1B, c0_1B);			\
  c1_1B = spu_madd(a12, b2_1B, c1_1B);			\
  c2_1B = spu_madd(a22, b2_1B, c2_1B);			\
  c3_1B = spu_madd(a32, b2_1B, c3_1B);			\
  c0_0B = spu_madd(a03, b3_0B, c0_0B);			\
  c1_0B = spu_madd(a13, b3_0B, c1_0B);			\
  c2_0B = spu_madd(a23, b3_0B, c2_0B);			\
  c3_0B = spu_madd(a33, b3_0B, c3_0B);			\
  c0_1B = spu_madd(a03, b3_1B, c0_1B);			\
  c1_1B = spu_madd(a13, b3_1B, c1_1B);			\
  c2_1B = spu_madd(a23, b3_1B, c2_1B);			\
  c3_1B = spu_madd(a33, b3_1B, c3_1B);			\
}

#define Stores4RegSetB(OFFSET)					\
{								\
  *((volatile vector float *)(ptrC+OFFSET)) = c0_0B;		\
  *((volatile vector float *)(ptrC+M+OFFSET)) = c1_0B;		\
  *((volatile vector float *)(ptrC+2*M+OFFSET)) = c2_0B;	\
  *((volatile vector float *)(ptrC+3*M+OFFSET)) = c3_0B;	\
  *((volatile vector float *)(ptrC+OFFSET+4)) = c0_1B;		\
  *((volatile vector float *)(ptrC+M+OFFSET+4)) = c1_1B;	\
  *((volatile vector float *)(ptrC+2*M+OFFSET+4)) = c2_1B;	\
  *((volatile vector float *)(ptrC+3*M+OFFSET+4)) = c3_1B;	\
}

static void MatInit_MxM(volatile float *blkC, volatile float *blkA, volatile float *blkB)
{
  unsigned int i;
  volatile float *ptrA, *ptrB, *ptrC;

  vector float a0, a1, a2, a3;

  vector float a00, a01, a02, a03;
  vector float a10, a11, a12, a13;
  vector float a20, a21, a22, a23;
  vector float a30, a31, a32, a33;

  vector float b0_0A, b1_0A, b2_0A, b3_0A;
  vector float c0_0A, c1_0A, c2_0A, c3_0A;
  vector float b0_1A, b1_1A, b2_1A, b3_1A;
  vector float c0_1A, c1_1A, c2_1A, c3_1A;

  vector float b0_0B, b1_0B, b2_0B, b3_0B;
  vector float c0_0B, c1_0B, c2_0B, c3_0B;
  vector float b0_1B, b1_1B, b2_1B, b3_1B;
  vector float c0_1B, c1_1B, c2_1B, c3_1B;

  vector float b0_0C, b1_0C, b2_0C, b3_0C;
  vector float c0_0C, c1_0C, c2_0C, c3_0C;
  vector float b0_1C, b1_1C, b2_1C, b3_1C;
  vector float c0_1C, c1_1C, c2_1C, c3_1C;

  for(i=0; i<M; i+=4){
    ptrA = &blkA[i*M];
    ptrB = &blkB[0];
    ptrC = &blkC[i*M];
    a0 = *((volatile vector float *)(ptrA));
    a1 = *((volatile vector float *)(ptrA+M));
    a2 = *((volatile vector float *)(ptrA+2*M));
    a3 = *((volatile vector float *)(ptrA+3*M));
    a00 = spu_shuffle(a0, a0, pat0);
    a01 = spu_shuffle(a0, a0, pat1);
    a02 = spu_shuffle(a0, a0, pat2);
    a03 = spu_shuffle(a0, a0, pat3);
    a10 = spu_shuffle(a1, a1, pat0);
    a11 = spu_shuffle(a1, a1, pat1);
    a12 = spu_shuffle(a1, a1, pat2);
    a13 = spu_shuffle(a1, a1, pat3);
    a20 = spu_shuffle(a2, a2, pat0);
    a21 = spu_shuffle(a2, a2, pat1);
    a22 = spu_shuffle(a2, a2, pat2);
    a23 = spu_shuffle(a2, a2, pat3);
    a30 = spu_shuffle(a3, a3, pat0);
    a31 = spu_shuffle(a3, a3, pat1);
    a32 = spu_shuffle(a3, a3, pat2);
    a33 = spu_shuffle(a3, a3, pat3);
    Loads4RegSetAClr(0);
    Ops4RegSetAClr();
    Loads4RegSetBClr(8);
    StageCBAclr(0);
    StageACBclr(8);
    StageBACclr(16);
    StageCBAclr(24);
    StageACBclr(32);
    StageBACclr(40);
    StageMISCclr();
    StageCBA(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(4,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(8,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    StageMISC(12,12);
    StageCBAmod(0,16);
    StageACB(8,16);
    StageBAC(16,16);
    StageCBA(24,16);
    StageACB(32,16);
    StageBAC(40,16);
    StageMISC(16,16);
    StageCBAmod(0,20);
    StageACB(8,20);
    StageBAC(16,20);
    StageCBA(24,20);
    StageACB(32,20);
    StageBAC(40,20);
    StageMISC(20,20);
    StageCBAmod(0,24);
    StageACB(8,24);
    StageBAC(16,24);
    StageCBA(24,24);
    StageACB(32,24);
    StageBAC(40,24);
    StageMISCmod(24,24);
    StageCBA(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(28,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(32,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    StageMISC(36,12);
    StageCBAmod(0,16);
    StageACB(8,16);
    StageBAC(16,16);
    StageCBA(24,16);
    StageACB(32,16);
    StageBAC(40,16);
    StageMISC(40,16);
    StageCBAmod(0,20);
    StageACB(8,20);
    StageBAC(16,20);
    StageCBA(24,20);
    StageACB(32,20);
    StageBAC(40,20);
    StageMISC(44,20);
    StageCBAmod(0,24);
    StageACB(8,24);
    StageBAC(16,24);
    StageCBA(24,24);
    StageACB(32,24);
    StageBAC(40,24);
    StageMISCmod(48,24);
    StageCBA(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(52,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(56,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    Ops4RegSetB();
    Stores4RegSetA(48);
    Stores4RegSetB(56);
  }
}

static void MatMult_MxM(volatile float *blkC, volatile float *blkA, volatile float *blkB)
{
  unsigned int i;
  volatile float *ptrA, *ptrB, *ptrC;

  vector float a0, a1, a2, a3;

  vector float a00, a01, a02, a03;
  vector float a10, a11, a12, a13;
  vector float a20, a21, a22, a23;
  vector float a30, a31, a32, a33;

  vector float b0_0A, b1_0A, b2_0A, b3_0A;
  vector float c0_0A, c1_0A, c2_0A, c3_0A;
  vector float b0_1A, b1_1A, b2_1A, b3_1A;
  vector float c0_1A, c1_1A, c2_1A, c3_1A;

  vector float b0_0B, b1_0B, b2_0B, b3_0B;
  vector float c0_0B, c1_0B, c2_0B, c3_0B;
  vector float b0_1B, b1_1B, b2_1B, b3_1B;
  vector float c0_1B, c1_1B, c2_1B, c3_1B;

  vector float b0_0C, b1_0C, b2_0C, b3_0C;
  vector float c0_0C, c1_0C, c2_0C, c3_0C;
  vector float b0_1C, b1_1C, b2_1C, b3_1C;
  vector float c0_1C, c1_1C, c2_1C, c3_1C;

  for(i=0; i<M; i+=4){
    ptrA = &blkA[i*M];
    ptrB = &blkB[0];
    ptrC = &blkC[i*M];
    a0 = *((volatile vector float *)(ptrA));
    a1 = *((volatile vector float *)(ptrA+M));
    a2 = *((volatile vector float *)(ptrA+2*M));
    a3 = *((volatile vector float *)(ptrA+3*M));
    a00 = spu_shuffle(a0, a0, pat0);
    a01 = spu_shuffle(a0, a0, pat1);
    a02 = spu_shuffle(a0, a0, pat2);
    a03 = spu_shuffle(a0, a0, pat3);
    a10 = spu_shuffle(a1, a1, pat0);
    a11 = spu_shuffle(a1, a1, pat1);
    a12 = spu_shuffle(a1, a1, pat2);
    a13 = spu_shuffle(a1, a1, pat3);
    a20 = spu_shuffle(a2, a2, pat0);
    a21 = spu_shuffle(a2, a2, pat1);
    a22 = spu_shuffle(a2, a2, pat2);
    a23 = spu_shuffle(a2, a2, pat3);
    a30 = spu_shuffle(a3, a3, pat0);
    a31 = spu_shuffle(a3, a3, pat1);
    a32 = spu_shuffle(a3, a3, pat2);
    a33 = spu_shuffle(a3, a3, pat3);
    Loads4RegSetA(0);
    Ops4RegSetA();
    Loads4RegSetB(8);
    StageCBA(0,0);
    StageACB(8,0);
    StageBAC(16,0);
    StageCBA(24,0);
    StageACB(32,0);
    StageBAC(40,0);
    StageMISC(0,0);
    StageCBAmod(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(4,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(8,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    StageMISC(12,12);
    StageCBAmod(0,16);
    StageACB(8,16);
    StageBAC(16,16);
    StageCBA(24,16);
    StageACB(32,16);
    StageBAC(40,16);
    StageMISC(16,16);
    StageCBAmod(0,20);
    StageACB(8,20);
    StageBAC(16,20);
    StageCBA(24,20);
    StageACB(32,20);
    StageBAC(40,20);
    StageMISC(20,20);
    StageCBAmod(0,24);
    StageACB(8,24);
    StageBAC(16,24);
    StageCBA(24,24);
    StageACB(32,24);
    StageBAC(40,24);
    StageMISCmod(24,24);
    StageCBA(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(28,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(32,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    StageMISC(36,12);
    StageCBAmod(0,16);
    StageACB(8,16);
    StageBAC(16,16);
    StageCBA(24,16);
    StageACB(32,16);
    StageBAC(40,16);
    StageMISC(40,16);
    StageCBAmod(0,20);
    StageACB(8,20);
    StageBAC(16,20);
    StageCBA(24,20);
    StageACB(32,20);
    StageBAC(40,20);
    StageMISC(44,20);
    StageCBAmod(0,24);
    StageACB(8,24);
    StageBAC(16,24);
    StageCBA(24,24);
    StageACB(32,24);
    StageBAC(40,24);
    StageMISCmod(48,24);
    StageCBA(0,4);
    StageACB(8,4);
    StageBAC(16,4);
    StageCBA(24,4);
    StageACB(32,4);
    StageBAC(40,4);
    StageMISC(52,4);
    StageCBAmod(0,8);
    StageACB(8,8);
    StageBAC(16,8);
    StageCBA(24,8);
    StageACB(32,8);
    StageBAC(40,8);
    StageMISC(56,8);
    StageCBAmod(0,12);
    StageACB(8,12);
    StageBAC(16,12);
    StageCBA(24,12);
    StageACB(32,12);
    StageBAC(40,12);
    Ops4RegSetB();
    Stores4RegSetA(48);
    Stores4RegSetB(56);
  }
}


