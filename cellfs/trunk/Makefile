SYSNAME:=${shell uname}
SYSNAME!=uname

all: 
	make -C libspfs
	make -C libspu
	make -C cellfs
	make -C tests

clean:
	rm -f *~
	rm -f include/*~
	make -C libspfs clean
	make -C libspu clean
	make -C cellfs clean
	make -C tests clean

