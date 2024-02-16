SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-g -L../libstrutil -lstrutil -L../libspclient -lspclient -L../libspfs -lspfs
HFILES=$(INCDIR)/spfs.h $(INCDIR)/spclient.h $(INCDIR)/strutil.h ixvm.h

FS=ixvm

OFILES=\
	file.o\
	tspawn.o\
	ufs.o\
	ixvm.o\
	xen.o\
	qemu.o\

all: $(FS)

ixvm: $(OFILES) $(HFILES) Makefile
	$(CC) -o ixvm $(CFLAGS) $(OFILES) $(LFLAGS)

install:
	for i in $(FS) ; do cp $$i /usr/local/sbin ; done

clean:
	rm -f *.o *~ core* $(FS)

%.c: $(HFILES) Makefile

$(FS): ../libspfs/libspfs.a ../libspclient/libspclient.a ../libstrutil/libstrutil.a

%.o: %.c $(HFILES) Makefile
	$(CC) $(CFLAGS) -c $*.c

