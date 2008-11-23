#! /bin/sh
#
# Modified check_sensors to check the alarm status of an Adaptec 3200S RAID
# controller.
#
# Scott Lambert -- lambert@lambertfam.org
#
# Tested on FreeBSD 4.7 with the adptfbsd_323.tgz package installed.  This 
# package installs all it's programs into /usr/dpt.
#

PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

PROGNAME=`basename $0`
PROGPATH=`echo $0 | sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION=`echo '$Revision: 302 $' | sed -e 's/[^0-9.]//g'`

. $PROGPATH/utils.sh

RAIDUTIL_CMD="/usr/dpt/raidutil -A ?"

print_usage() {
	echo "Usage: $PROGNAME"
}

print_help() {
	print_revision $PROGNAME $REVISION
	echo ""
	print_usage
	echo ""
	echo "This plugin checks alarm status of Adaptec 3200S RAID controller."
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
		raidutiloutput=`$RAIDUTIL_CMD 2>&1`
		status=$?
		if test "$1" = "-v" -o "$1" = "--verbose"; then
			echo ${raidutiloutput}
		fi
		if test ${status} -eq 127; then
			echo "RAIDUTIL UNKNOWN - command not found (did you install raidutil?)"
			exit -1
		elif test ${status} -ne 0 ; then
			echo "WARNING - raidutil returned state $status"
			exit 1
		fi
		if echo ${raidutiloutput} | egrep On > /dev/null; then
			echo RAID CRITICAL - RAID alarm detected!
			exit 2
		else
			echo raid ok
			exit 0
		fi
		;;
esac
