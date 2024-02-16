SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
NPFSDIR=../npfs
CFLAGS=-Wall -g -I $(INCDIR) -I$(NPFSDIR)/include -DSYSNAME=$(SYSNAME)
LFLAGS=-L. -L../libstrutil -lstrutil -L$(NPFSDIR)/libnpclient -lnpclient -L$(NPFSDIR)/libnpfs -lnpfs -lcrypto -lm -lpthread
HFILES=$(NPFSDIR)/include/npfs.h $(NPFSDIR)/include/npclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h

CMD=\
	statfs\

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)

install:
	mkdir -p $(INSTALLPREFIX)/bin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/bin ; done

$(CMD): $(NPFSDIR)/libnpclient/libnpclient.a ../libstrutil/libstrutil.a $(NPFSDIR)/libnpfs/libnpfs.a Makefile

statfs: statfs.o xauth.o
	$(CC) $(CFLAGS) -o statfs xauth.o statfs.o $(LFLAGS)

%.o: %.c $(HFILES)
	$(CC) $(CFLAGS) -c $*.c
