ALL=spe-ramtest spe-ufstest spe-ramtest3 spe-ramtest4 spe-pipetest spe-euler spe-sgemv spe-mmul geneuler genmmul vermmul gensgemv
CC=gcc
SPUCC=spu-gcc
CFLAGS=-O2 -I../include
LFLAGS=-L../libspu -lspu
DEP=../libspu/libspu.a ../include/libspu.h
all: $(ALL)

spe-ramtest: spe-ramtest.c $(DEP);
	$(SPUCC) $(CFLAGS) -o spe-ramtest spe-ramtest.c $(LFLAGS)

spe-ramtest3: spe-ramtest3.c $(DEP);
	$(SPUCC) $(CFLAGS) -o spe-ramtest3 spe-ramtest3.c $(LFLAGS)

spe-ramtest4: spe-ramtest4.c $(DEP);
	$(SPUCC) $(CFLAGS) -o spe-ramtest4 spe-ramtest4.c $(LFLAGS)

spe-ufstest: spe-ufstest.c ../libspu/libspu.a ../include/libspu.h
	$(SPUCC) $(CFLAGS) -o spe-ufstest spe-ufstest.c $(LFLAGS)

spe-euler: spe-euler.c ../libspu/libspu.a ../include/libspu.h
	$(SPUCC) $(CFLAGS) -o spe-euler spe-euler.c $(LFLAGS)

spe-sgemv: spe-sgemv.c ../libspu/libspu.a ../include/libspu.h
	$(SPUCC) $(CFLAGS) -o spe-sgemv spe-sgemv.c $(LFLAGS)

spe-mmul: spe-mmul.c ../libspu/libspu.a ../include/libspu.h
	$(SPUCC) -Os -I../include -o spe-mmul spe-mmul.c $(LFLAGS)

spe-pipetest: spe-pipetest.c ../libspu/libspu.a ../include/libspu.h
	$(SPUCC) -I../include -o spe-pipetest spe-pipetest.c $(LFLAGS)

clean:
	rm -f *.o *~ core* $(ALL)

%.c: $(HFILES) Makefile

%.o: %.c $(HFILES) Makefile
	$(CC) $(CFLAGS) -c $*.c

