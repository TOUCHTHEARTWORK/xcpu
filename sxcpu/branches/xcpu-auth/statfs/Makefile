SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)

HFILES=npfs.h npclient.h xcpu.h strutil.h statfs.h
LIBS= libstrutil.a libnpclient.a   libnpfs.a
LFLAGS=-g $(subst :, ,$(join $(LIBS:%.a=-L../%), $(subst lib,, $(LIBS:%.a=:-l%)))) -lm -lpthread -lcrypto

vpath %.h ../include
vpath %.a ../libxcpu:../libstrutil:../libspclient:../libxauth:../libspfs:../libnpfs:../libnpclient:

CMD=statfs
OFILES=$(CMD:%=%.o) queue.o util.o xauth.o

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)

install:
	mkdir -p $(INSTALLPREFIX)/bin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/bin ; done


$(CMD): %: $(OFILES) $(LIBS)
	$(CC) $< util.o xauth.o -o $@ $(LFLAGS)

$(OFILES): %.o: %.c $(HFILES)
	$(CC) -c $(CFLAGS) $< -o $@


