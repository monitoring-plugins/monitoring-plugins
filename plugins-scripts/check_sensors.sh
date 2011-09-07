#!/bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

PROGNAME=`basename $0`
PROGPATH=`echo $0 | sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION="@NP_VERSION@"

. $PROGPATH/utils.sh


print_usage() {
	echo "Usage: $PROGNAME" [--ignore-fault]
}

print_help() {
	print_revision $PROGNAME $REVISION
	echo ""
	print_usage
	echo ""
	echo "This plugin checks hardware status using the lm_sensors package."
	echo ""
	support
	exit 0
}

case "$1" in
	--help)
		print_help
		exit 0
		;;
	-h)
		print_help
		exit 0
		;;
	--version)
   	print_revision $PROGNAME $REVISION
		exit 0
		;;
	-V)
		print_revision $PROGNAME $REVISION
		exit 0
		;;
	*)
		sensordata=`sensors 2>&1`
		status=$?
		if test "$1" = "-v" -o "$1" = "--verbose"; then
			echo ${sensordata}
		fi
		if test ${status} -eq 127; then
			echo "SENSORS UNKNOWN - command not found (did you install lmsensors?)"
			exit -1
		elif test ${status} -ne 0 ; then
			echo "WARNING - sensors returned state $status"
			exit 1
		fi
		if echo ${sensordata} | egrep ALARM > /dev/null; then
			echo SENSOR CRITICAL - Sensor alarm detected!
			exit 2
		elif echo ${sensordata} | egrep FAULT > /dev/null \
		    && test "$1" != "-i" -a "$1" != "--ignore-fault"; then
			echo SENSOR UNKNOWN - Sensor reported fault
			exit 3
		fi
		echo sensor ok
		exit 0
		;;
esac
