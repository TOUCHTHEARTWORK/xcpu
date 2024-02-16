#!/bin/sh

# start 10 local copies and mount them on /mnt/xterm
# to be run as root

maxrun=9

if [ "$1" ]; then
	if [ "$1" = "-h" ]; then
		echo "usage: $0 [-h] [n]"
		echo "   where n is the number of xcpusrv instances to start"
		exit 1
	fi
	if [ "$1" -gt 0 ]; then 
		maxrun="$1"
	fi
fi

modprobe 9p2000

for i in `seq 1 $maxrun`; do
	port=$((20000+$i))
	echo -n "$port "
	xsrv/o.xsrv tcp\!*\!$port  > /dev/null 2>&1
	if [ ! -d /mnt/xcpu/$i/xcpu ]; then
		mkdir -p /mnt/xcpu/$i/xcpu
	fi
	mount -t 9P -o noextend,port=$port 127.1 /mnt/xcpu/$i/xcpu/
done

echo
