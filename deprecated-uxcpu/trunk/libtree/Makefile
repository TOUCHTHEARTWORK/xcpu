SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/npfs.h ../include/npclient.h ../include/tree.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	tree.o\
	npcread.o\
	npcwrite.o\
	read.o\
	write.o\

libtree.a: $(LIBFILES)
	ar rc libtree.a $(LIBFILES)
	ranlib libtree.a

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
