SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/libspu.h spuimpl.h
SPUCC=spu-gcc
AS=spu-as
CFLAGS=-Wall -g -I ../include -O2

LIBFILES=\
	cor.o\
	dma.o\
	error.o\
	fakelabel.o\
	fsys.o\
	gotolabel.o\
	log.o\
	np.o\
	open.o\
	read.o\
	remove.o\
	seek.o\
	setlabel.o\
	walk.o\
	write.o\

libspu.a: $(LIBFILES)
	ar rc libspu.a $(LIBFILES)
	ranlib libspu.a

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(SPUCC) $(CFLAGS) -c $*.c
