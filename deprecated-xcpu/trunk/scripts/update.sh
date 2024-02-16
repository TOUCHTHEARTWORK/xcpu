#!/bin/sh

# update our copy of p9p from the original

DIR1=$1
DIR2=$2
if [ "x$DIR1" = "x" ]; then
	echo "usage: $0 /path/to/p9p [/path/to/xcpu]"
	exit 1
fi

if test -z "$DIR2"; then
	DIR2=$PLAN9
fi

if [ $DIR1 = $DIR2 ]; then
	echo "target and destination paths are the same"
	exit 1
fi

for i in `find $DIR2/src -type f | grep -v '\.svn' | grep -v 'CVS' | grep -v $DIR2/src/cmd/mkfile`; do
	j=`echo $i | sed "s,$DIR2,$DIR1,"`

	if ! cmp $j $i > /dev/null; then
		echo cp $j $i
	fi
done
