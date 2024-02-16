#!/bin/sh
# a simple shell to demonstrate an xsh execution.

echo $#

exit 0;
n=$1
c=$2 
b=`which $c`
shift
w=`./o.openclone /mnt/xcpu/$n/xcpu/clone` << EOF
cd /mnt/xcpu/$n/xcpu/$w
cp $b exec
echo $c > argv
cat stdout&
echo exec > ctl
echo eof > ctl
EOF

