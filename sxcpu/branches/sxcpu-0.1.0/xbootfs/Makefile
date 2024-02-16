SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-static -L. -L../libstrutil -lstrutil -L../libspclient -lspclient -L../libspfs -lspfs -lm -pthread
HFILES=$(INCDIR)/spfs.h $(INCDIR)/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h xbootfs.h

CMD=\
	xbootfs\

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)


$(CMD): ../libspfs/libspfs.a ../libspclient/libspclient.a Makefile

xbootfs: xbootfs.o
	$(CC) $(CFLAGS) -o xbootfs xbootfs.o $(LFLAGS)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
