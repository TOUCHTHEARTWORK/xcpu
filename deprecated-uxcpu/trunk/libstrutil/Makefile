SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/npfs.h ../include/strutil.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	quotestrdup.o\
	unquotestr.o\
	tokenize.o\
	cutstr.o\

libstrutil.a: $(LIBFILES)
	ar rc libstrutil.a $(LIBFILES)
	ranlib libstrutil.a

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
