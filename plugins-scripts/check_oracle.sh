#!/bin/sh
#
# latigid010@yahoo.com
# 01/06/2000
#
#  This Nagios plugin was created to check remote or local TNS
#  status and check local Database status.
#
#  Add the following lines to your object config file (i.e. commands.cfg)
#         command[check-tns]=/usr/local/nagios/libexec/check_ora 1 $ARG$
#         command[check-oradb]=/usr/local/nagios/libexec/check_ora 2 $ARG$
#
#
# Usage: 
#      To check TNS Status:  ./check_ora 1 <Oracle Sid or Hostname/IP address>
#  To Check local database:  ./check_ora 2 <ORACLE_SID>
#
# I have the script checking for the Oracle PMON process and 
# the sgadefORACLE_SID.dbf file.
# 
#
# If you have any problems check that you have the $ORACLE_HOME
# enviroment variable set, have $ORACLE_HOME/bin in your PATH, and
# dont forget about your tnsnames.ora file.  when checking Local
# Database status your ORACLE_SID is case sensitive.
#

PROGNAME=`basename $0`
PROGPATH=`echo $0 | sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION=`echo '$Revision$' | sed -e 's/[^0-9.]//g'`

. $PROGPATH/utils.sh


print_usage() {
  echo "Usage:"
  echo "  $PROGNAME --tns <Oracle Sid or Hostname/IP address>"
  echo "  $PROGNAME --db <ORACLE_SID>"
  echo "  $PROGNAME --help"
  echo "  $PROGNAME --version"
}

print_help() {
	print_revision $PROGNAME $REVISION
	echo ""
	print_usage
	echo ""
	echo "Check remote or local TNS status and check local Database status"
	echo ""
  echo "--tns=SID/IP Address"
  echo "   Check remote TNS server"
  echo "--db=SID"
  echo "   Check local database (search /bin/ps for PMON process and check"
	echo "   filesystem for sgadefORACLE_SID.dbf"
  echo "--help"
	echo "   Print this help screen"
  echo "--version"
	echo "   Print version and license information"
	echo ""
  echo "If the plugin doesn't work, check that the $ORACLE_HOME environment"
	echo "variable is set, that $ORACLE_HOME/bin is in your PATH, and the"
  echo "tnsnames.ora file is locatable and is properly configured."
  echo ""
  echo "When checking Local Database status your ORACLE_SID is case sensitive."
  echo ""
	support
}

case "$1" in
1)
    cmd='--tns'
    ;;
2)
    cmd='--db'
    ;;
*)
    cmd="$1"
    ;;
esac

case "$cmd" in
--tns)
    export tnschk=` tnsping $2`
    export tnschk2=` echo  $tnschk | grep -c OK`
    export tnschk3=` echo $tnschk | cut -d\( -f7 | sed y/\)/" "/`
    if [ ${tnschk2} -eq 1 ] ; then 
	echo "OK - reply time ${tnschk3} from $2"
	exit 0
    else
	echo "No TNS Listener on $2"
	exit $STATE_CRITICAL
    fi
    ;;
--db)
    export pmonchk=`ps -ef | grep -v grep | grep ${2} | grep -c pmon`
    if [ -e $ORACLE_HOME/dbs/sga*${2}* ] ; then
	if [ ${pmonchk} -eq 1 ] ; then
    export utime=`ls -la $ORACLE_HOME/dbs/sga*$2* | cut -c 43-55`
	    echo "${2} OK - running since ${utime}"
	    exit $STATE_OK
	fi
    else
	echo "${2} Database is DOWN"
	exit $STATE_CRITICAL
    fi
    ;;
--help)
		print_help
    exit $STATE_OK
    ;;
-h)
		print_help
    exit $STATE_OK
    ;;
--version)
		print_revision $PLUGIN $REVISION
    exit $STATE_OK
    ;;
-V)
		print_revision $PLUGIN $REVISION
    exit $STATE_OK
    ;;
*)
    print_usage
		exit $STATE_UNKNOWN
esac
