#!/bin/bash
#
# chkconfig: 345 75 25
# description: Starts master on one or more ports, using config files in /etc/sysconfig/xbootfs
#

source /etc/rc.d/init.d/functions
XBOOTFS=/usr/sbin/xbootfs

# functions:
function parse_config {
    eval $(LANG=C fgrep "KERNEL=" $i)
    eval $(LANG=C fgrep "INITRD=" $i)
    eval $(LANG=C fgrep "BASEPORT=" $i)
    eval $(LANG=C fgrep "DEBUG=" $i)
    
    if [ -z "$BASEPORT" ] ; then
	[ "x$1" = "xstart" ] && {
	    echo -n $"$0: $i requires BASEPORT to be set"; echo_warning; echo
	}
	unset KERNEL INITRD BASEPORT DEBUG
	continue
    fi;
    
    if [ -z "$PORT" ] ; then 
	PORT="$BASEPORT"
    else 
	if [ $PORT -ge $BASEPORT ]; then
	    [ "x$1" = "xstart" ] && {
		echo -n $"$0: $i BASEPORT $PORT already used"; echo_warning; echo
	    }
	    unset KERNEL INITRD BASEPORT DEBUG
	    return
	fi;
	PORT="$BASEPORT"
    fi;
    
    [ "x$1" = "xlist" ] && {
	echo -en "`basename $2 | sed 's/image-//'`:\t"
    }
    
    if [ -z $DEBUG ]; then
	DEBUG=0
    fi;
    
    if [ $DEBUG -ge 1 ]; then
	DEBUG=0
    fi;
    
    [ "x$KERNEL" != "x" ] && [ -f $KERNEL ] && {
	[ "x$1" = "xlist" ] && {
	    echo -en "KERNEL=$PORT\t"
	}
	[ "x$1" = "xstart" ] && {
	    echo -n $"$0: $i kernel on port $PORT"
	    daemon $"$XBOOTFS -D $DEBUG -p $PORT -f $KERNEL"
	    RETVAL=$?
	    [ $RETVAL -eq 0 ] && touch /var/lock/subsys/xbootfs.$i.kernel.$PORT
	    started=$(($started + 1))
	    echo
	}
	PORT=$((PORT + 1))
    }
    
    unset RETVAL
    
    [ "x$INITRD" != "x" ] && [ -f $INITRD ] && {
	[ "x$1" = "xlist" ] && {
	    echo -en "INITRD=$PORT\t"
	}
	[ "x$1" = "xstart" ] && {
	    echo -n $"$0: $i initrd on port $PORT"
	    daemon $"$XBOOTFS -D $DEBUG -p $PORT -f $INITRD"
	    RETVAL=$?
	    [ $RETVAL -eq 0 ] && touch /var/lock/subsys/xbootfs.$i.initrd.$PORT
	    started=$(($started + 1))
	}
	echo
    }
    unset RETVAL
    unset KERNEL INITRD BASEPORT DEBUG    
}


RETVAL=0

if [ ! -x $XBOOTFS ] ; then
    echo -n $"$XBOOTFS is not installed"; echo_failure; echo
    exit 1
fi

if [ ! -d /etc/sysconfig/xbootfs ]; then
    echo -n $"$0: no images to start"; echo_warning; echo
    exit 0
fi

cd /etc/sysconfig/xbootfs

images=`ls image-* 2>/dev/null | LANG=C egrep -v '(rpmsave|rpmorig|rpmnew|bak)' | \
    LANG=C egrep 'image-[A-Za-z0-9\._-]+$' | sed 's/ //'`

case "$1" in
    start)
    [ -z "$images" ] && {
	echo -n $"$0: no images to start"; echo_warning; echo
	exit 0
    }

    echo "Starting xbootfs:"
    started=0

    for i in $images; do
	parse_config $1 $i
    done
    
    [ $started -ge 1 ] || {
	echo -n $"$0: no valid images in /etc/sysconfig/xbootfs"; echo_warning; echo
    }
    ;;    
    list)
    	[ -z "$images" ] && {
	    echo -n $"$0: no images to list"; echo
	    exit 0
	}

	for i in $images; do
	    parse_config $1 $i
	done
	;;
    stop)
        echo -n "Stopping xbootfs: "
        killproc $XBOOTFS
        RETVAL=$?
	echo
        [ $RETVAL -eq 0 ] && rm -f /var/lock/subsys/xbootfs.*
        ;;
    restart)
        $0 stop
        RETVAL=$?
        [ $RETVAL -eq 0 ] && $0 start
        RETVAL=$?
        ;;
    status)
        status $XBOOTFS
        RETVAL=$?
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
esac

exit $RETVAL
