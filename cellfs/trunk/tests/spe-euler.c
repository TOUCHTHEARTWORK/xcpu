#include <stdio.h>
#include <spu_intrinsics.h>
#include "libspu.h"

#define PREF "#U/tmp/euler/"
#define NELEM(x) (sizeof(x)/(sizeof(x[0])))

typedef struct Ctx Ctx;

struct Ctx {
	int		startidx;
	int		count;
	vector float 	pos[1024];
	vector float 	vel[1024];
	float 		invmass[1024];
};

enum {
	Ncor = 4,
};

int nparticles;
int nsteps;
int nspu;
vector float dt_v;
vector float force_v;

int posfd;
int velfd;
int massfd;
int ppcor;
int ppspu;

char stack[Ncor][4096] __attribute__((aligned(128)));
Ctx ctx[Ncor];
char buf[32];
char *Eformat = "invalid format";

char *
str2int(char *str, int *val)
{
	int n;
	char *s;

	s = str;
	n = 0;
	while (*s>='0' && *s<='9') {
		n = n*10 + (*s - '0');
		s++;
	}

	*val = n;
	return s;
		
}

char *
str2float(char *s, float *val)
{
	int n, m;
	float v;

	n = 0;
	m = 0;
	s = str2int(s, &n);
	if (*s == '.') {
		s++;
		s = str2int(s, &m);
	}

	v = m;
	while (v > 0)
		v /= 10.0;

	*val = n + v;
	return s;
}

int
readstr(char *name, char *buf, int bufsize)
{
	int n, fd;

	fd = spc_open(name, Oread);
	if (fd < 0)
		goto error;

	n = spc_read(fd, (u8 *) buf, bufsize - 1);
	spc_close(fd);
	if (n < 0)
		goto error;

	buf[n] = '\0';
	return 0;

error:
	return -1;
}

int
readint(char *name, int *val)
{
	int n;
	char *s;

	if (readstr(name, buf, sizeof(buf)) < 0)
		goto error;

	s = str2int(buf, val);
	if (*s != '\0' && *s != '\n') {
		sp_werror(Eformat);
		return -1;
	}

	return 0;

error:
	return -1;
}

int
readfloat(char *name, float *val)
{
	float f;
	char *s;

	if (readstr(name, buf, sizeof(buf)) < 0)
		goto error;

	s = str2float(buf, val);
	if (*s != '\0' && *s != '\n') {
		sp_werror(Eformat);
		return -1;
	}

	return 0;

error:
	return -1;
}

int
readvector(char *name, vector float *val)
{
	int n, fd;

	fd = spc_open(name, Oread);
	if (fd < 0)
		return -1;

	n = spc_read(fd, (u8 *) val, sizeof(*val));
	spc_close(fd);
	if (n < 0)
		return -1;

	return 0;
}

int
euler(Ctx *ctx)
{
	int i, j, n;
	int vsize, msize;
	int voff, moff;
	vector float dt_inv_mass_v;
	char *ename;

	for(i = 0; i < ctx->count; i+= NELEM(ctx->pos)) {
		n = NELEM(ctx->pos);
		if (n > ctx->count)
			n = ctx->count - i;

		vsize = n * sizeof(ctx->pos[0]);
		msize = n * sizeof(ctx->invmass[0]);
		voff = (i + ctx->startidx) * sizeof(ctx->pos[0]);
		moff = (i + ctx->startidx) * sizeof(ctx->invmass[0]);

		if (spc_pread(posfd, (u8 *) ctx->pos, vsize, voff) < 0)
			goto error;

		if (spc_pread(velfd, (u8 *) ctx->vel, vsize, voff) < 0)
			goto error;

		if (spc_pread(massfd, (u8 *) ctx->invmass, msize, moff) < 0)
			goto error;

		for(j = 0; j < n; j++) {
			ctx->pos[j] = spu_madd(ctx->vel[j], dt_v, ctx->pos[j]);
			dt_inv_mass_v = spu_mul(dt_v, spu_splats(ctx->invmass[j]));
			ctx->vel[j] = spu_madd(dt_inv_mass_v, force_v, ctx->vel[j]);
		}

		if (spc_pwrite(posfd, (u8 *)ctx->pos, vsize, voff) < 0)
			goto error;

		if (spc_pwrite(velfd, (u8 *)ctx->vel, vsize, voff) < 0)
			goto error;
	}

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}

void
eulercor(void *a)
{
	int i;
	Ctx *ctx;

	ctx = a;
	for(i = 0; i < nsteps; i++) {
		euler(ctx);
	}
}

void
cormain(unsigned long long spuid, unsigned long long arg, unsigned long long env)
{
	int i, id;
	float dt;
	char *ename;

	id = (int) arg;
	posfd = spc_open(PREF"pos", Ordwr);
	if (posfd < 0)
		goto error;

	velfd = spc_open(PREF"vel", Ordwr);
	if (velfd < 0)
		goto error;

	massfd = spc_open(PREF"invmass", Ordwr);
	if (massfd < 0)
		goto error;

	if (readint(PREF"nparticles", &nparticles) < 0)
		goto error;

	if (readint(PREF"nspu", &nspu) < 0)
		goto error;

	if (readint(PREF"nsteps", &nsteps) < 0)
		goto error;

	if (readfloat(PREF"dt", &dt) < 0)
		goto error;

	if (readvector(PREF"force", &force_v) < 0)
		goto error;

	dt_v = spu_splats(dt);

	ppspu = nparticles / nspu;
	ppspu += nparticles%nspu?1:0;
	ppcor = ppspu / Ncor;
	ppcor += ppspu%Ncor?1:0;
	spc_log("id %d nspu %d nsteps %d ppspu %d ppcor %d\n", id, nspu, nsteps, ppspu, ppcor);
	for(i = 0; i < Ncor; i++) {
		ctx[i].startidx = id*ppspu + ppcor*i;
		ctx[i].count = ppcor;
		if (ctx[i].startidx + ctx[i].count > nparticles)
			ctx[i].count = nparticles - ctx[i].startidx;

		if (mkcor(eulercor, &ctx[i], &stack[i], sizeof(stack[i])) < 0)
			goto error;
	}

	return;

error:
	sp_rerror(&ename);
	spc_log("Error: %s\n", ename);
}
