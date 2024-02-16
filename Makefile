# This enables automatic library shipping. 
LIBELF:=1
export LIBELF

SYSNAME:=${shell uname}
SYSNAME!=uname
INSTALLPREFIX:=/usr/local
LIBELF:=1
export INSTALLPREFIX
export LIBELF

LIBS=\
	spfs/libspfs/libspfs.a\
	libstrutil/libstrutil.a\
	libxauth/libxauth.a\
	spfs/libspclient/libspclient.a\
	npfs/libnpfs/libnpfs.a\
	npfs/libnpclient/libnpclient.a\
	libxcpu/libxcpu.a\

all: binaries

clean:
	rm -f *~
	make -C spfs clean
	make -C libstrutil clean
	make -C libxauth clean
	make -C npfs clean
	make -C libxcpu clean
	make -C xcpufs clean
	make -C statfs clean
	make -C utils clean
	make -C xget clean

install:
	make -C spfs install
	make -C libstrutil install
	make -C libxauth install
	make -C npfs install
	make -C libxcpu install
	make -C xcpufs install
	make -C statfs install
	make -C utils install
	make -C xget install

installman:
	mkdir -p $(INSTALLPREFIX)/share/man/man1
	mkdir -p $(INSTALLPREFIX)/share/man/man4
	cp man/man1/*.1 $(INSTALLPREFIX)/share/man/man1/
	cp man/man4/*.4 $(INSTALLPREFIX)/share/man/man4/

installscripts:
	make -C misc install

%.a:
	make -C spfs/libspfs
	make -C libstrutil
	make -C libxauth
	make -C spfs/libspclient
	make -C npfs/libnpfs
	make -C npfs/libnpclient
	make -C libxcpu

libs: $(LIBS)

binaries: libs
	make -C xcpufs
	make -C statfs
	make -C utils
	make -C xget

xcpufs: libs
	make -C xcpufs

utils: libs
	make -C utils

statfs: libs
	make -C statfs

xget: libs
	make -C xget
