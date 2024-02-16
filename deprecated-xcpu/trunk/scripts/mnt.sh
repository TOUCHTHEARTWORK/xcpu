#!/bin/sh

# mount n remote xcpusrv's from host 'n'
maxrun=9
host=""

if [ "$1" ]; then
	if [ "$1" = "-h" ]; then
		echo "usage: $0 host [n]"
		echo "   where n is the number of xcpusrv instances to mount"
		echo "         host is the node where they're started"
		exit 1
	fi
	host="$1"
	shift 1
	if [ "$1" -gt 0 ]; then 
		maxrun="$1"
	fi
else
	echo "usage: $0 host [n]"
	echo "   where n is the number of xcpusrv instances to mount"
	echo "         host is the node where they're started"
	exit 1
fi
modprobe 9p2000
for i in `seq 0 $maxrun`; do
	port=$((20000+$i))
	echo -n "$host:$port "
	if [ ! -d /mnt/xcpu/$i/xcpu ]; then
		mkdir -p /mnt/xcpu/$i/xcpu
	fi
	mount -t 9P -o noextend,port=$port $host /mnt/xcpu/$i/xcpu/
done

echo
