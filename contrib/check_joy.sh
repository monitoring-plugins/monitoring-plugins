#! /bin/sh

PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin

PROGNAME=`basename $0`
PROGPATH=`echo $0 | sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION=`echo '$Revision: 2 $' | sed -e 's/[^0-9.]//g'`
STATUS=""

. $PROGPATH/utils.sh


print_usage() {
        echo "Usage: $PROGNAME /dev/js<#> <button #>"
}

print_help() {
        print_revision $PROGNAME $REVISION
        echo ""
        print_usage
        echo ""
        echo "This plugin checks a joystick button status using the "
        echo "joyreadbutton utility from the joyd package."
        echo ""
        support
        exit 0
}

if [ $# -ne 2 ]; then
        print_usage
        exit 0
fi

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
        /dev/js*)
                joyreadbutton $1 $2 1>&1 1>/dev/null
                STATUS=$?
                if [ "$STATUS" -eq 0 ]; then
                        echo OK
                        exit 0
                elif [ "$STATUS" -eq 1 ];then
                        echo CRITICAL
                        exit 2
                else
                        echo UNKNOWN
                        exit -1
                fi
                ;;
        *)
                print_usage
                exit 0
                ;;
esac
