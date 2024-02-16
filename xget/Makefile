SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
SPFSDIR=../spfs
CFLAGS=-Wall -g -I $(INCDIR) -I$(SPFSDIR)/include -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -L../libstrutil -lstrutil -L$(SPFSDIR)/libspclient -lspclient -L$(SPFSDIR)/libspfs -lspfs -lz
HFILES=$(SPFSDIR)/include/spfs.h $(SPFSDIR)/include/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h xget.h

CMD=xget
OFILES=\
	xget.o\


all: $(CMD)

xget: $(OFILES) $(HFILES) Makefile
	$(CC) -o xget $(CFLAGS) $(OFILES) $(LFLAGS)

xget.static: $(OFILES) $(HFILES) Makefile
	$(CC) -static -o xget.static $(CFLAGS) $(OFILES) $(LFLAGS) -lz

install:
	mkdir -p $(INSTALLPREFIX)/sbin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/sbin ; done

clean:
	rm -f *.o *.a *~ core* $(CMD)

%.c: $(HFILES) Makefile

$(CMD): $(SPFSDIR)/libspfs/libspfs.a $(SPFSDIR)/libspclient/libspclient.a Makefile

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
