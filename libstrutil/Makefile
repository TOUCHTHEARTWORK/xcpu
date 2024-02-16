SYSNAME:=${shell uname}
SYSNAME!=uname
MULTILIBPATH=${shell test -d /lib64 && echo lib64 || echo lib}
INCDIR=../include
HFILES=$(INCDIR)/strutil.h
CFLAGS=-Wall -g -I$(INCDIR)

LIBFILES=\
	quotestrdup.o\
	unquotestr.o\
	tokenize.o\
	cutstr.o\
	range.o\

libstrutil.a: $(LIBFILES)
	ar rc libstrutil.a $(LIBFILES)
	ranlib libstrutil.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/$(MULTILIBPATH)
	cp ../include/strutil.h $(INSTALLPREFIX)/include
	cp libstrutil.a $(INSTALLPREFIX)/$(MULTILIBPATH)

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
