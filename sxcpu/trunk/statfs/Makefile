SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
NPFSDIR=../npfs
SPFSDIR=../spfs
CFLAGS=-Wall -g -I $(INCDIR) -I$(NPFSDIR)/include -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -L../libstrutil -lstrutil -L$(NPFSDIR)/libnpclient -lnpclient -L$(NPFSDIR)/libnpfs -lnpfs -lcrypto -lm -lpthread
HFILES=$(SPFSDIR)/include/spfs.h $(SPFSDIR)/include/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h

CMD=\
	statfs\

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)

install:
	mkdir -p $(INSTALLPREFIX)/bin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/bin ; done

$(CMD): $(NPFSDIR)/libnpclient/libnpclient.a \
	../libstrutil/libstrutil.a \
	$(SPFSDIR)/libspfs/libspfs.a $(SPFSDIR)/libspclient/libspclient.a Makefile

statfs: statfs.o xauth.o
	$(CC) $(CFLAGS) -o statfs xauth.o statfs.o $(LFLAGS)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
