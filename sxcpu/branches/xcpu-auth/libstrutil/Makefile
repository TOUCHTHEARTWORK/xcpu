SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/spfs.h ../include/strutil.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	quotestrdup.o\
	unquotestr.o\
	tokenize.o\
	cutstr.o\

libstrutil.a: $(LIBFILES)
	ar rc libstrutil.a $(LIBFILES)
	ranlib libstrutil.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/lib
	cp ../include/strutil.h $(INSTALLPREFIX)/include
	cp libstrutil.a $(INSTALLPREFIX)/lib

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
