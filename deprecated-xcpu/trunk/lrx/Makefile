CC=gcc
OBJ=lrx.o thread.o misc.o
RMS=${OBJ} o.lrx ~*
CFLAGS= -g -Wall -pedantic

o.lrx:	lrx.o thread.o misc.o
	${CC} ${CFLAGS} -pthread -o o.lrx lrx.o thread.o misc.o
lrx.o:	lrx.c
	${CC} ${CFLAGS} -c lrx.c
thread.o: thread.c
	${CC} ${CFLAGS} -c thread.c
misc.o: misc.c
	${CC} ${CFLAGS} -c misc.c
clean:
	rm -rf ${RMS}
    
