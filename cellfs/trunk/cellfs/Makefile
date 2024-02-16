SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)
LFLAGS=-g -L../libspfs -lspfs -lspe2 -lpthread
HFILES=$(INCDIR)/spfs.h
FS=cellfs
OFILES=logfs.o main.o pipefs.o ramfs.o rootfs.o ufs.o ufs2.c

all: $(FS) 

cellfs: $(OFILES) $(HFILES) Makefile
	$(CC) -o cellfs $(CFLAGS) $(OFILES) $(LFLAGS)

clean:
	rm -f *.o *~ core* $(FS) spe-ramtest spe-ufstest

%.c: $(HFILES) Makefile

$(FS): ../libspfs/libspfs.a

%.o: %.c $(HFILES) Makefile
	$(CC) $(CFLAGS) -c $*.c

