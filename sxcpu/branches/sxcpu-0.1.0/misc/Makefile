SYSNAME:=${shell uname}
SYSNAME!=uname

INITSCRIPT=xcpufs.sh

all: $(INITSCRIPT)

install:
	for i in $(INITSCRIPT) ; do cp $$i $(INSTALLPREFIX)/etc/init.d ; done

clean:
	rm -f *.o *~ core*


