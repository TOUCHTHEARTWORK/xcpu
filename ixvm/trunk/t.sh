#!/bin/sh

mount -t 9p 127.0.0.1 /mnt/9 -o port=7777,debug=1,uname=root
cd /mnt/9
tail -f clone &
sleep 2
cd 0
kill %1
cp /tmp/vm fs/vmfile
echo dev hda /home/lucho/tmp/disk.img > ctl
echo net 0 00:11:22:33:44:55 > ctl
echo power on freeze > ctl
echo loadvm vmfile > ctl
echo unfreeze > ctl


