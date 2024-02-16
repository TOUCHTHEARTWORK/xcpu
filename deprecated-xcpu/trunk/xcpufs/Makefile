SYSNAME:=${shell uname}
SYSNAME!=uname
CFLAGS=-Wall -g -I include -DSYSNAME=$(SYSNAME)
LFLAGS=-lpthread -Llibnpfs -lnpfs

FS=xcpufs

all: $(FS)

xcpufs: xcpufs.o str.o pipe.o ufs.o Makefile
	$(CC) -o xcpufs $(CFLAGS) xcpufs.o str.o pipe.o ufs.o $(LFLAGS)

clean:
	rm -f *.o $(FS) 
	make -C libnpfs clean

%.c: include/npfs.h Makefile

libnpfs/libnpfs.a:
	make -C libnpfs

$(FS): libnpfs/libnpfs.a

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c

