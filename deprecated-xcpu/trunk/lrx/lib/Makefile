CC=gcc
MAIN_OBJ= libexample.o
OBJ=thread.o misc.o lrx.o
RMS=${OBJ} ${MAIN_OBJ} libxcpu.so libexample ~*
CFLAGS= -g -Wall -pedantic

libexample:	libexample.o libxcpu.so
	${CC} ${CFLAGS} -L./ -lxcpu -o libexample libexample.o
libxcpu.so: ${OBJ}
	${CC} ${CFLAGS} -shared -pthread -o libxcpu.so ${OBJ}
lrx.o:	../lrx.c
	${CC} ${CFLAGS} -fPIC -c ../lrx.c
thread.o: ../thread.c
	${CC} ${CFLAGS} -fPIC -c ../thread.c
misc.o: ../misc.c
	${CC} ${CFLAGS} -fPIC -c ../misc.c
libexample.o:	libexample.c
	${CC} ${CFLAGS} -c libexample.c
clean:
	rm -rf ${RMS}
    
