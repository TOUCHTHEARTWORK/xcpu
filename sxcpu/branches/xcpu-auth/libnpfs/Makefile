SYSNAME:=${shell uname}
SYSNAME!=uname
HFILES=../include/npfs.h npfsimpl.h
CFLAGS=-Wall -g -I ../include

LIBFILES=\
	conn.o\
	error.o\
	fcall.o\
	fdtrans.o\
	fidpool.o\
	file.o\
	fmt.o\
	mount-$(SYSNAME).o\
	np.o\
	pipesrv.o\
	pvusers.o\
	socksrv.o\
	srv.o\
	trans.o\
	user.o\
	uxusers.o\

libnpfs.a: $(LIBFILES)
	ar rc libnpfs.a $(LIBFILES)
	ranlib libnpfs.a

install:
	mkdir -p $(INSTALLPREFIX)/include
	mkdir -p $(INSTALLPREFIX)/lib
	cp ../include/npfs.h $(INSTALLPREFIX)/include
	cp libnpfs.a $(INSTALLPREFIX)/lib


clean:
	rm -f *.o *.a *~

$(LIBFILES): Makefile $(HFILES)

%.o: %.c 
	$(CC) $(CFLAGS) -c $*.c
