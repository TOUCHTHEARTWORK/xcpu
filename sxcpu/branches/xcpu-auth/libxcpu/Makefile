SYSNAME:=${shell uname}
SYSNAME!=uname
LIBELFINC=/opt/local/include
HFILES=../include/libxcpu.h xcpuimpl.h ../include/xcpu.h
CFLAGS=-Wall -g -I ../include -I$(LIBELFINC)

LIBFILES=\
	command.o\
	file.o\
	ldd.o\
	node.o\
	proc.o\
	session.o\
	user.o\
	util.o\

libxcpu.a: $(LIBFILES)
	ar rc libxcpu.a $(LIBFILES)
	ranlib libxcpu.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/lib
	cp ../include/libxcpu.h $(INSTALLPREFIX)/include
	cp ../include/xcpu.h $(INSTALLPREFIX)/include
	cp libxcpu.a $(INSTALLPREFIX)/lib

clean:
	rm -f *.o *.a *~ core.*

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
