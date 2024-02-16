SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
SPFSDIR=../spfs
CFLAGS=-Wall -g -I $(INCDIR) -I $(SPFSDIR)/include -DSYSNAME=$(SYSNAME)
LFLAGS=-g -L../libstrutil -lstrutil -L$(SPFSDIR)/libspclient -lspclient -L$(SPFSDIR)/libspfs -lspfs -L../libxauth -lxauth -lcrypto
HFILES=$(SPFSDIR)/include/spfs.h $(SPFSDIR)/include/spclient.h $(INCDIR)/strutil.h $(INCDIR)/libxauth.h xcpufs.h

FS=xcpufs xcpufs.static
OFILES=\
	pipe.o\
	proc-$(SYSNAME).o\
	ufs.o\
	xauth.o\
	xcpufs.o\

all: $(FS)

xcpufs: $(OFILES) $(HFILES) Makefile
	$(CC) -o xcpufs $(CFLAGS) $(OFILES) $(LFLAGS)

xcpufs.static: $(OFILES) $(HFILES) Makefile
	$(CC) -static -o xcpufs.static  $(CFLAGS) $(OFILES) $(LFLAGS) -ldl

install:
	mkdir -p $(INSTALLPREFIX)/sbin
	for i in $(FS) ; do cp $$i $(INSTALLPREFIX)/sbin ; done

clean:
	rm -f *.o *~ core* $(FS)

%.c: $(HFILES) Makefile

$(FS): $(SPFSDIR)/libspfs/libspfs.a $(SPFSDIR)/libspclient/libspclient.a ../libstrutil/libstrutil.a

%.o: %.c $(HFILES) Makefile
	$(CC) $(CFLAGS) -c $*.c

