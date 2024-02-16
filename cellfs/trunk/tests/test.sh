rm -f /tmp/btest
dd if=/dev/zero of=/tmp/btest bs=1048576 count=128

nspu=$1
ncor=$2

		t1=$((time -p ../cellfs/cellfs -a $ncor $(for i in `seq 1 $nspu`; do echo spe-ramtest3; done)) 2>&1 | grep real | sed -e 's/real //')
                sleep 5
                t2=$((time -p ../cellfs/cellfs -a $ncor $(for i in `seq 1 $nspu`; do echo spe-ramtest3; done)) 2>&1 | grep real | sed -e 's/real //')
                sleep 5
                t3=$((time -p ../cellfs/cellfs -a $ncor $(for i in `seq 1 $nspu`; do echo spe-ramtest3; done)) 2>&1 | grep real | sed -e 's/real //')
		t=`echo 2k $t1 $t2 $t3 + + 3 / p | dc` 
		s=`echo 2k 134217728 $ncor '*' $nspu '*' $t '/' 1024 / 1024 / p | dc`
		echo nspu=$nspu ncor=$ncor time=$t speed=$s GB/s
