SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -L../libstrutil -lstrutil -L../libspclient -lspclient -L../libspfs -lspfs -lm
HFILES=$(INCDIR)/spfs.h $(INCDIR)/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h xbootfs.h

CMD=xbootfs
OFILES=\
	xbootfs.o\

all: $(CMD)

xbootfs: $(OFILES) $(HFILES) Makefile
	$(CC) -o xbootfs $(CFLAGS) $(OFILES) $(LFLAGS)

install:
	mkdir -p $(INSTALLPREFIX)/sbin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/sbin ; done

clean:
	rm -f *.o *.a *~ core* $(CMD)

%.c: $(HFILES) Makefile

$(CMD): ../libspfs/libspfs.a ../libspclient/libspclient.a Makefile

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
