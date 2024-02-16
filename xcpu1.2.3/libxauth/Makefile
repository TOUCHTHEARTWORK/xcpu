SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
INCDIR=../include
SPFSDIR=../spfs
HFILES=$(SPFSDIR)/include/spfs.h $(INCDIR)/libxauth.h
CFLAGS=-Wall -g -I$(INCDIR) -I$(SPFSDIR)/include

LIBFILES=\
	xauth.o\

libxauth.a: $(LIBFILES)
	ar rc libxauth.a $(LIBFILES)
	ranlib libxauth.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/libxauth.h $(INSTALLPREFIX)/include
	cp libxauth.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~

xtest: xtest.c libxauth.a
	$(CC) -o xtest $(CFLAGS) xtest.c -L$(SPFSDIR)/libspfs -lspfs -L. -lxauth -lcrypto

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
