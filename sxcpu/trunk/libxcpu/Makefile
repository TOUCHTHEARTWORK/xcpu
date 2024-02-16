SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
LIBELFINC=/opt/local/include
HFILES=../include/libxcpu.h xcpuimpl.h ../include/xcpu.h ../spfs/include/spclient.h ../spfs/include/spfs.h
CFLAGS=-Wall -g -I ../include -I$(LIBELFINC) -I../spfs/include

ifeq ($(LIBELF),1)
	CFLAGS+=-DXP_LDD
endif

LIBFILES=\
	command.o\
	file.o\
	ldd.o\
	node.o\
	proc.o\
	session.o\
	user.o\
	group.o\
	util.o\

libxcpu.a: $(LIBFILES)
	ar rc libxcpu.a $(LIBFILES)
	ranlib libxcpu.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/libxcpu.h $(INSTALLPREFIX)/include
	cp ../include/xcpu.h $(INSTALLPREFIX)/include
	cp libxcpu.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~ core.*

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
