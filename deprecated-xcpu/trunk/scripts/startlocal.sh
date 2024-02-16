#!/bin/sh

# start n local copies without mounting them

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

for i in `seq 0 $maxrun`; do
	port=$((20000+$i))
	echo -n "$port "
	./o.xcpusrv -s tcp\!*\!$port -9 -D 0  > /dev/null 2>&1
done

echo
