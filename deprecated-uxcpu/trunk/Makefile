SYSNAME:=${shell uname}
SYSNAME!=uname

all: 
	make -C libnpfs
	make -C libcopyfile
	make -C libnpclient
	make -C libstrutil
	make -C xcpufs
	make -C rx

clean:
	rm -f *~
	make -C libnpfs clean
	make -C libcopyfile clean
	make -C libnpclient clean
	make -C libstrutil clean
	make -C xcpufs clean
	make -C rx clean

