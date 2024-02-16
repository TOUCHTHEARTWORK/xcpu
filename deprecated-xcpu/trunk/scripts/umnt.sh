#!/bin/sh

ustr=""

for i in `mount | grep "type 9P" | awk '{print $3}'`; do
	echo -n "$i "
	ustr="$ustr $i"
done

if [ -z "$ustr" ]; then 
	exit 0; 
fi

echo

echo "unmounting..."
umount $ustr
