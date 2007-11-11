#!/bin/sh

TOTAL_ACTUAL=0
TOTAL_COVERED=0
TOTAL_SOURCE=0
TOTAL_PERCENT=0
NOT_TESTED=""

MODULE=$1
shift

process ()
{
	if [ -e ".libs/packagekitd" ]; then
		gcov $1 &> /dev/null
	elif [ -e "src/.libs/packagekitd" ]; then
		gcov $1 &> /dev/null
	elif [ -e ".libs/libpackagekit.la" ]; then
		gcov $1 -o .libs &> /dev/null
	elif [ -e "libpackagekit/.libs/libpackagekit.la" ]; then
		gcov $1 -o .libs &> /dev/null
	else
		return
	fi
	if [ ! -e $1.gcov ]; then
		NOT_TESTED="$1,$NOT_TESTED"
		return
	fi
	SOURCE=`cat $1 |wc -l`
	ACTUAL=`grep -v "        -:" $1.gcov  |wc -l`
	NOT_COVERED=`grep "    #####:" $1.gcov  |wc -l`
	COVERED=$(($ACTUAL - $NOT_COVERED))
	if [ $ACTUAL -ne 0 ]; then
		PERCENT=$((100 * $COVERED / $ACTUAL))
	else
		PERCENT=0
	fi

	TOTAL_SOURCE=$(($TOTAL_SOURCE + $SOURCE))
	TOTAL_ACTUAL=$(($TOTAL_ACTUAL + $ACTUAL))
	TOTAL_COVERED=$(($TOTAL_COVERED + $COVERED))

	echo -n "$1"

	n=${#1}
	while [ $n -lt 55 ] ; do
		echo -n " "
		n=$(($n + 1))
	done

	echo -n " : "

	if [ $PERCENT -lt 10 ] ; then
		echo -n "  $PERCENT%"
	elif [ $PERCENT -lt 100 ] ; then
		echo -n " $PERCENT%"
	else
		echo -n "100%"
	fi

	echo " ($COVERED of $ACTUAL)"
}

echo "=============================================================================="
echo "Test coverage for module $MODULE:"
echo "=============================================================================="

while [ $# -gt 0 ] ; do

	case "$1" in
	"pk-main.c"|"pk-marshal.c"|"pk-security-dummy.c"|"pk-backend-python.c")
		#ignore these
		;;
	*)
		process $1
		;;
	esac
	shift
done

if [ $TOTAL_ACTUAL -ne 0 ]; then
	TOTAL_PERCENT=$((100 * $TOTAL_COVERED / $TOTAL_ACTUAL))
fi
if [ -n "NOT_TESTED" ]; then
	echo "NOT TESTED = $NOT_TESTED"
fi

echo
echo "Source lines          : $TOTAL_SOURCE"
echo "Actual statements     : $TOTAL_ACTUAL"
echo "Executed statements   : $TOTAL_COVERED"
echo "Test coverage         : $TOTAL_PERCENT%"
echo

