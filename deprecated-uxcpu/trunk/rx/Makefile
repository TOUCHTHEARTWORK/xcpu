SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -lrx -L../libcopyfile -lcopyfile -L../libnpclient -lnpclient -L../libstrutil -lstrutil -L../libnpfs -lnpfs -lpthread
HFILES=$(INCDIR)/npfs.h $(INCDIR)/npclient.h $(INCDIR)/copyfile.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h

CMD=\
	rx\

LIBFILES=\
	rxcmd.o\
	rxfile.o\
	rxsession.o\
	rxutil-$(SYSNAME).o\

all: $(CMD)

librx.a: $(LIBFILES)
	ar rc librx.a $(LIBFILES)
	ranlib librx.a

clean:
	rm -f *.o *.a *~ core* $(CMD)

$(LIBFILES): Makefile $(HFILES)

$(CMD): ../libcopyfile/libcopyfile.a ../libnpclient/libnpclient.a ../libnpfs/libnpfs.a librx.a

rx: rx.o 
	$(CC) $(CFLAGS) -o rx rx.o $(LFLAGS)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
