SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -L../libstrutil -lstrutil -L../libnpclient -lnpclient -L../libnpfs -lnpfs -lcrypto -lm -lpthread
HFILES=$(INCDIR)/spfs.h $(INCDIR)/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h

CMD=\
	statfs\

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)

install:
	mkdir -p $(INSTALLPREFIX)/bin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/bin ; done

$(CMD): ../libspfs/libspfs.a ../libspclient/libspclient.a Makefile

statfs: statfs.o xauth.o
	$(CC) $(CFLAGS) -o statfs xauth.o statfs.o $(LFLAGS)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
