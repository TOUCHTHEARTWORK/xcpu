SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/npfs.h ../include/npclient.h ../include/copyfile.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	npcopy.o\
	copy.o\

libcopyfile.a: $(LIBFILES)
	ar rc libcopyfile.a $(LIBFILES)
	ranlib libcopyfile.a

clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
