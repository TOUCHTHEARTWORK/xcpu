SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-L../libcopyfile -lcopyfile -L../libnpclient -lnpclient -L../libstrutil -lstrutil -L../libnpfs -lnpfs -lpthread 
HFILES=$(INCDIR)/npfs.h $(INCDIR)/npclient.h $(INCDIR)/copyfile.h $(INCDIR)/strutil.h xcpufs.h

FS=xcpufs

all: $(FS)

xcpufs: xcpufs.o pipe.o ufs.o tspawn.o xauth.o Makefile
	$(CC) -o xcpufs $(CFLAGS) xcpufs.o pipe.o ufs.o tspawn.o xauth.o $(LFLAGS)

clean:
	rm -f *.o *~ core* $(FS)

%.c: $(HFILES) Makefile

$(FS): ../libnpfs/libnpfs.a ../libnpclient/libnpclient.a ../libcopyfile/libcopyfile.a ../libstrutil/libstrutil.a

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c

