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
	libspfs/libspfs.a\
	libstrutil/libstrutil.a\
	libxauth/libxauth.a\
	libspclient/libspclient.a\
	libnpfs/libnpfs.a\
	libnpclient/libnpclient.a\
	libxcpu/libxcpu.a\

all: binaries

clean:
	rm -f *~
	make -C libspfs clean
	make -C libstrutil clean
	make -C libxauth clean
	make -C libspclient clean
	make -C libnpfs clean
	make -C libnpclient clean
	make -C libxcpu clean
	make -C xcpufs clean
	make -C xbootfs clean
	make -C statfs clean
	make -C utils clean

install:
	make -C libspfs install
	make -C libstrutil install
	make -C libxauth install
	make -C libspclient install
	make -C libnpfs install
	make -C libnpclient install
	make -C libxcpu install
	make -C xcpufs install
	make -C xbootfs install
	make -C statfs install
	make -C utils install

installman:
	mkdir -p $(INSTALLPREFIX)/share/man/man1
	mkdir -p $(INSTALLPREFIX)/share/man/man4
	cp man/man1/*.1 $(INSTALLPREFIX)/share/man/man1/
	cp man/man4/*.4 $(INSTALLPREFIX)/share/man/man4/

installscripts:
	make -C misc install

%.a:
	make -C libspfs
	make -C libstrutil
	make -C libxauth
	make -C libspclient
	make -C libnpfs
	make -C libnpclient
	make -C libxcpu

libs: $(LIBS)

binaries: libs
	make -C xcpufs
	make -C xbootfs
	make -C statfs
	make -C utils

xbootfs: libs
	make -C xbootfs

xcpufs: libs
	make -C xcpufs

utils: libs
	make -C utils

statfs: libs
	make -C statfs
