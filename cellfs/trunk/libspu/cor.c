#include <stdio.h>
#include "libspu.h"
#include "spuimpl.h"

typedef struct Cor Cor;
typedef struct Margs Margs;

struct Cor {
	int flags;	/* C* */
	void *arg;
	void (*fn)(void *arg);
	void *stack;
};

enum { 
	Cnotused = 0,
	Cready,
	Cwait,
	Cmax,
};

struct Margs {
	unsigned long long	spuid;
	unsigned long long	argv;
	unsigned long long	env;
};

static Label mainlab __attribute__((aligned(128)));
static Cor cor[Maxcor] __attribute__((aligned(128)));
static Label lab[Maxcor] __attribute__((aligned(128)));
static int cur = -1;
static int ncor = 0;
//static char mainstack[8193] __attribute__((aligned(128)));

static void sched(void);
static int initstack(void *stackbase, int stacksize);

int
corid(void)
{
	return cur;
}

void
corwait()
{
	Cor *cp;

//	fprintf(stderr, "corwait %d\n", cur);
	cp = &cor[cur];
	cp->flags = Cwait;
	sched();
}

void
corready(int n)
{
//	fprintf(stderr, "corready %d\n", n);
	cor[n].flags = Cready;
}

void
yield(void)
{
	Cor *cp;

	cp = &cor[cur];
	cp->flags = Cready;
	sched();
}

static void
sched(void)
{
	int i;
	Cor *cp;

//	fprintf(stderr, "sched cur %d ncor %d flags %d\n", cur, ncor, cor[cur].flags);
	cp = &cor[cur];
	spc_check(0);

again:
	i = (cur + 1) % ncor;
	while (i != cur && cor[i].flags != Cready) {
//		fprintf(stderr, "\tsched %d %d\n", i, cor[i].flags);
		i = (i + 1) % ncor;
	}

//	n = cor[i].flags;
//	fprintf(stderr, "sched i %d flags %d\n", i, n);
	if (cor[i].flags != Cready) {
		/* block until we get data from the PPU */
//		fprintf(stderr, "sched: block\n");
		spc_check(1);
		goto again;
	}

//	fprintf(stderr, "sched switch from %d to %d\n", cur, i);
	if (cur == i)
		return;

	if(setlabel(&lab[cur]) == 1)
		return;

	cur = i;
	cp = &cor[cur];
	gotolabel(&lab[cur]);
}

void
terminate(void)
{
	int i;

//	fprintf(stderr, "terminate %d\n", cur);
	cor[cur].flags = Cnotused;
	for(i = ncor - 1; i >= 0; i--)
		if (cor[i].flags != Cnotused)
			break;

//	ncor = i + 1;
	if (i < 0)
		gotolabel(&mainlab);

	sched();
}

static Label *auxlab;
static void
mkcor_aux(void *a)
{
	Cor *cp;

//	checkstack("mkcor_aux");
	cp = &cor[cur];
	if(setlabel(&lab[cur]) == 1){
		(*cp->fn)(cp->arg);
		terminate();
	}

	gotolabel(auxlab);
}

int
mkcor(void (*fn)(void*), void *arg, void *stackbase, int stacksize)
{
	int i, old;

	for(i = 0; i < Maxcor; i++) {
		if (cor[i].flags == Cnotused)
			break;
	}

	if (i == Maxcor) {
		sp_werror("too many cors");
		return -1;
	}

	if(i >= ncor)
		ncor = i + 1;

	cor[i].flags = Cready;
	cor[i].stack = stackbase;
	cor[i].fn = fn;
	cor[i].arg = arg;

	if (initstack(stackbase, stacksize) < 0)
		return -1;

	old = cur;
	cur = i;
	fakelabel(&lab[i], mkcor_aux, stackbase, stacksize);

	if (old < 0)
		auxlab = &mainlab;
	else
		auxlab = &lab[old];

	if (setlabel(auxlab) == 0) {
//		checkstack("mkcor setlabel");
		gotolabel(&lab[i]);
	}

	cur = old;
//	checkstack("mkcor end");
	return i;
}

void
main_aux(void *a)
{
	Margs *m;

	m = a;
	spc_init();
	spc_log_init();
	cormain(m->spuid, m->argv, m->env);
}

int
main(unsigned long long spuid, unsigned long long argv, unsigned long long env)
{
	int n;
	Margs m;
	unsigned int stkbase, stksize;
	char *ename;

	m.spuid = spuid;
	m.argv = argv;
	m.env = env;
	stksize = 8192; //  + 1024;
	stkbase = (unsigned int) &ename - stksize - 512;
	stkbase &= ~15;
//	stkbase = mainstack;
	

	n = mkcor(main_aux, &m, (void *) stkbase, stksize);
	if (n < 0)
		return -1;

	if (setlabel(&mainlab) == 0) {
		cur = n;
		gotolabel(&lab[n]);
	} 
	return 0;
}

static int
initstack(void *stackbase, int stacksize)
{
	unsigned int *p, *ep;

	if ((int) stackbase % 16) {
		sp_werror("stack pointer not aligned");
		return -1;
	}

	if (stacksize % 16) {
		sp_werror("stack size not aligned");
		return -1;
	}

	p = (unsigned int *) stackbase;
	ep = (unsigned int *) ((char *) stackbase + stacksize);
	while (p < ep)
		*(p++) = 0xcafebabe;

	return 0;
}

/*
void
checkstack(char *func)
{
	int i, n;
	unsigned int *p;

	for(i = 0; i < ncor; i++) {
		if (cor[i].flags > Cmax)
			fprintf(stderr, "%s: cor %d has wrong flags %d\n", func, i, cor[i].flags);

		if (cor[i].flags == Cnotused || !cor[i].stack)
			continue;

		for(p = cor[i].stack; *p == 0xcafebabe; p++)
			;

		n = (char *) p - (char *) cor[i].stack;
		if (n < 128)
			fprintf(stderr, "%s: cor %d stack %p left %d\n", func, i, cor[i].stack, n);
	}
}
*/
