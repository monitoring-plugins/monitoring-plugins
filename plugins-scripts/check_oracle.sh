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
  echo "  $PROGNAME --login <ORACLE_SID>"
  echo "  $PROGNAME --oranames <Hostname>"
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
  echo "--login=SID"
  echo "   Attempt a dummy login and alert if not ORA-01017: invalid username/password"
  echo "--oranames=Hostname"
  echo "   Check remote Oracle Names server"
  echo "--help"
	echo "   Print this help screen"
  echo "--version"
	echo "   Print version and license information"
	echo ""
  echo "If the plugin doesn't work, check that the ORACLE_HOME environment"
	echo "variable is set, that ORACLE_HOME/bin is in your PATH, and the"
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

# Information options
case "$cmd" in
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
esac

# Hunt down a reasonable ORACLE_HOME
if [ -z "$ORACLE_HOME" ] ; then
	# Adjust to taste
	for oratab in /var/opt/oracle/oratab /etc/oratab
	do
	[ ! -f $oratab ] && continue
	ORACLE_HOME=`IFS=:
		while read SID ORACLE_HOME junk;
		do
			if [ "$SID" = "$2" ] ; then
				echo $ORACLE_HOME;
				exit;
			fi;
		done < $oratab`
	[ -n "$ORACLE_HOME" ] && break
	done
fi
# Last resort
[ -z "$ORACLE_HOME" -a -d $PROGPATH/oracle ] && ORACLE_HOME=$PROGPATH/oracle

if [ -z "$ORACLE_HOME" -o ! -d "$ORACLE_HOME" ] ; then
	echo "Cannot determine ORACLE_HOME for sid $2"
	exit $STATE_UNKNOWN
fi
PATH=$PATH:$ORACLE_HOME/bin
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$ORACLE_HOME/lib
export ORACLE_HOME PATH LD_LIBRARY_PATH

case "$cmd" in
--tns)
    tnschk=` tnsping $2`
    tnschk2=` echo  $tnschk | grep -c OK`
    if [ ${tnschk2} -eq 1 ] ; then 
	tnschk3=` echo $tnschk | sed -e 's/.*(//' -e 's/).*//'`
	echo "OK - reply time ${tnschk3} from $2"
	exit $STATE_OK
    else
	echo "No TNS Listener on $2"
	exit $STATE_CRITICAL
    fi
    ;;
--oranames)
    namesctl status $2 | awk '
    /Server has been running for:/ {
	msg = "OK: Up"
	for (i = 6; i <= NF; i++) {
	    msg = msg " " $i
	}
	status = '$STATE_OK'
    }
    /error/ {
	msg = "CRITICAL: " $0
	status = '$STATE_CRITICAL'
    }
    END {
	print msg
	exit status
    }'
    ;;
--db)
    pmonchk=`ps -ef | grep -v grep | grep ${2} | grep -c pmon`
    if [ ${pmonchk} -ge 1 ] ; then
	echo "${2} OK - ${pmonchk} PMON process(es) running"
	exit $STATE_OK
    #if [ -f $ORACLE_HOME/dbs/sga*${2}* ] ; then
	#if [ ${pmonchk} -eq 1 ] ; then
    #utime=`ls -la $ORACLE_HOME/dbs/sga*$2* | cut -c 43-55`
	    #echo "${2} OK - running since ${utime}"
	    #exit $STATE_OK
	#fi
    else
	echo "${2} Database is DOWN"
	exit $STATE_CRITICAL
    fi
    ;;
--login)
    loginchk=`sqlplus dummy/user@$2 < /dev/null`
    loginchk2=` echo  $loginchk | grep -c ORA-01017`
    if [ ${loginchk2} -eq 1 ] ; then 
	echo "OK - dummy login connected"
	exit $STATE_OK
    else
	loginchk3=` echo "$loginchk" | grep "ORA-" | head -1`
	echo "CRITICAL - $loginchk3"
	exit $STATE_CRITICAL
    fi
    ;;
*)
    print_usage
		exit $STATE_UNKNOWN
esac
