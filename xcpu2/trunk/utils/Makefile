SYSNAME:=${shell uname}
SYSNAME!=uname
INCDIR=../include
SPFSDIR=../spfs
CFLAGS=-Wall -g -I$(INCDIR) -I$(SPFSDIR)/include -DSYSNAME=$(SYSNAME)

HFILES=$(SPFSDIR)/include/spfs.h $(SPFSDIR)/include/spclient.h $(INCDIR)/xcpu.h $(INCDIR)/strutil.h $(INCDIR)/libxcpu.h
LIBS=\
	../libxcpu/libxcpu.a\
	../libstrutil/libstrutil.a\
	$(SPFSDIR)/libspclient/libspclient.a\
	$(SPFSDIR)/libspfs/libspfs.a\
	../libxauth/libxauth.a\

LFLAGS=-g\
	-L../libxcpu -L../libstrutil -L$(SPFSDIR)/libspclient -L$(SPFSDIR)/libspfs -L../libxauth\
	-lxcpu -lstrutil -lspclient -lspfs -lxauth\
	-lcrypto -lm

vpath %.h ../include $(SPFSDIR)/include
vpath %.a ../libxcpu:../libstrutil:$(SPFSDIR)/libspclient:$(SPFSDIR)/libspfs:../libxauth

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

$(OFILES): %.o: %.c $(HFILES) Makefile
	$(CC) -c $(CFLAGS) $< -o $@


