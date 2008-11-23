#! /bin/sh
#
# Tested on SuSE 9.1 Professional with the hwinfo-8.62-0.2 package installed.
# 
# Before you can run this plugin, you must do:
# /usr/sbin/hwinfo --short > /etc/hw.original
# add to cron job:
# /usr/sbin/hwinfo --short > /etc/hw.current 
# /usr/bin/diff /etc/hw.original /etc/hw.current > /tmp/hw.check
# 
#
# Rok Debevc -- rok.debevc@agenda.si
#
#
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

PROGNAME=`basename $0`
PROGPATH=`echo $0 | sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION=`echo '$Revision: 939 $' | sed -e 's/[^0-9.]//g'`

. $PROGPATH/utils.sh


print_usage() {
	echo "Usage: $PROGNAME"
}

print_help() {
	print_revision $PROGNAME $REVISION
	echo ""
	print_usage
	echo ""
	echo "This plugin checks hardware changes."
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
		if `du /tmp/hw.check | cut -c 1|grep "^[0]" > /dev/null` ; then
			echo No hardware is changed
                        exit 0
		else
			echo ***hardware is changed*** look into /tmp/hw.check
                        exit 2
		fi
		;;
esac

