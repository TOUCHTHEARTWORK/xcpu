#!/bin/sh

ustr=""

for i in `mount | grep 127.1 | awk '{print $3}'`; do
	echo -n "$i "
	ustr="$ustr $i"
done

if [ -z "$ustr" ]; then 
	exit 0; 
fi

echo

echo "unmounting..."
umount $ustr
echo "killing o.xcpusrv..."
killall -9 o.xsrv
