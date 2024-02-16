SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-g -L../libstrutil -lstrutil -L../libspclient -lspclient -L../libspfs -lspfs -L../libxauth -lxauth -lcrypto
HFILES=$(INCDIR)/spfs.h $(INCDIR)/spclient.h $(INCDIR)/strutil.h $(INCDIR)/libxauth.h xcpufs.h

FS=xcpufs
OFILES=\
	pipe.o\
	proc-$(SYSNAME).o\
	ufs.o\
	xauth.o\
	xcpufs.o\

all: $(FS)

xcpufs: $(OFILES) $(HFILES) Makefile
	$(CC) -o xcpufs $(CFLAGS) $(OFILES) $(LFLAGS)

install:
	mkdir -p $(INSTALLPREFIX)/sbin
	for i in $(FS) ; do cp $$i $(INSTALLPREFIX)/sbin ; done

clean:
	rm -f *.o *~ core* $(FS)

%.c: $(HFILES) Makefile

$(FS): ../libspfs/libspfs.a ../libspclient/libspclient.a ../libstrutil/libstrutil.a

%.o: %.c $(HFILES) Makefile
	$(CC) $(CFLAGS) -c $*.c

