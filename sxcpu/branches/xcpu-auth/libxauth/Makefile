SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/spfs.h ../include/libxauth.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	xauth.o\

libxauth.a: $(LIBFILES)
	ar rc libxauth.a $(LIBFILES)
	ranlib libxauth.a

install:
	cp ../include/libxauth.h /usr/local/include
	cp libxauth.a /usr/local/lib

clean:
	rm -f *.o *.a *~

xtest: xtest.c libxauth.a
	$(CC) -o xtest $(CFLAGS) xtest.c -L../libspfs -lspfs -L. -lxauth -lcrypto

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
