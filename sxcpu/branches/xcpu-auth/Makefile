SYSNAME:=${shell uname}
SYSNAME!=uname
INSTALLPREFIX:=/usr/local
export INSTALLPREFIX

all: 
	make -C libspfs
	make -C libstrutil
	make -C libxauth
	make -C libspclient
	make -C libnpfs
	make -C libnpclient
	make -C libxcpu
	make -C xcpufs
	make -C statfs
	make -C utils

clean:
	rm -f *~
	make -C libspfs clean
	make -C libstrutil clean
	make -C libxauth clean
	make -C libspclient clean
	make -C libxcpu clean
	make -C xcpufs clean
	make -C statfs clean
	make -C utils clean
	make -C libnpclient clean
	make -C libnpfs clean


install:
	make -C libspfs install
	make -C libstrutil install
	make -C libspclient install
	make -C libxcpu install
	make -C xcpufs install
	make -C statfs install
	make -C utils install

installman:
	mkdir -p $(INSTALLPREFIX)/share/man/man1
	mkdir -p $(INSTALLPREFIX)/share/man/man4
	cp man/man1/*.1 $(INSTALLPREFIX)/share/man/man1/
	cp man/man4/*.4 $(INSTALLPREFIX)/share/man/man4/
