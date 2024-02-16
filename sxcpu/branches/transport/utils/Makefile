SYSNAME:=${shell uname}
SYSNAME!=uname
LIBELFLIB=/opt/local/lib
INCDIR=../include
CFLAGS=-Wall -g -I $(INCDIR) -DSYSNAME=$(SYSNAME)

HFILES=spfs.h spclient.h xcpu.h strutil.h libxcpu.h
LIBS=libxcpu.a libstrutil.a libspclient.a libspfs.a libxauth.a
LFLAGS=-g $(subst :, ,$(join $(LIBS:%.a=-L../%), $(subst lib,, $(LIBS:%.a=:-l%)))) -L$(LIBELFLIB) -lcrypto -lm

ifeq ($(LIBELF), 1)
	LFLAGS+=-L$(LIBELFLIB) -lelf
endif


vpath %.h ../include
vpath %.a ../libxcpu:../libstrutil:../libspclient:../libspfs:../libxauth

CMD=xrx xps xstat xk xuserset xgroupset xgetent
OFILES=$(CMD:%=%.o)

all: $(CMD)

clean:
	rm -f *.o *.a *~ core* $(CMD)

install:
	mkdir -p $(INSTALLPREFIX)/bin
	for i in $(CMD) ; do cp $$i $(INSTALLPREFIX)/bin ; done


$(CMD): %: %.o $(LIBS)
	$(CC) -o $@ $< $(LFLAGS)

$(OFILES): %.o: %.c $(HFILES)
	$(CC) -c $(CFLAGS) $< -o $@


