#!/bin/sh
# This script is designed to be used by Nagios. It checks for the availability of both Microsoft SQL Server 7 and 2000.
#
# Requirements:
#
# FreeTDS 6.0+ (http://www.freetds.org/)
#
# It was written by Tom De Blende (tom.deblende@village.uunet.be) in 2003. 
#
# Version 1.0.
# Version 1.1: Rewritten the initial script so that it not only works from the CLI but also from within Nagios. Always helpful...
# Version 1.2: Grouped output so things look a bit better.
# Version 2.0: Rewritten the plugin to support version 6.0+ of FreeTDS. 
#              Removed sqsh requirement as version 6.0+ of FreeTDS now offers its own CLI client: tsql.
#              Older versions of FreeTDS are no longer supported.
#
#
# You might want to change these values:

tsqlcmd=`which tsql`
catcmd=`which cat`
grepcmd=`which grep`
rmcmd=`which rm`
mktempcmd=`which mktemp`
wccmd=`which wc`
sedcmd=`which sed`
trcmd=`which tr`
uniqcmd=`which uniq`

###################################################################################################################

hostname=$1
usr=$2
pswd=$3
srv=$4


if [ ! "$#" == "4" ]; then
        echo -e "\nYou did not supply enough arguments. \nUsage: $0 <host> <username> <password> <version> \n \n$0 checks Microsoft SQL Server connectivity. It works with versions 7 and 2000.\n\nYou need a working version of FreeTDS (http://www.freetds.org/) and tsql (included in FreeTDS 6.0+) to connect to the SQL server. \nIt was written by Tom De Blende (tom.deblende@village.uunet.be) in 2003. \n\nExample:\n $0 dbserver sa f00bar 2000\n" && exit "3"

elif [ $tsqlcmd == "" ]; then
	echo -e "tsql not found! Please verify you have a working version of tsql (included in the FreeTDS version 6.0+) and enter the full path in the script." && exit "3"

fi

exit="3"


# Creating the command file that contains the sql statement that has to be run on the SQL server.

tmpfile=`$mktempcmd /tmp/$hostname.XXXXXX`

if [ $srv == "7" ]; then
        spid=7
elif [ $srv == "2000" ]; then
        spid=50
else
	echo -e "$srv is not a supported MS SQL Server version!" && exit "3"
fi

echo -e "select loginame from sysprocesses where spid > $spid order by loginame asc\ngo" > $tmpfile


# Running tsql to get the results back.

resultfile=`$mktempcmd /tmp/$hostname.XXXXXX`
errorfile=`$mktempcmd /tmp/$hostname.XXXXXX`
$tsqlcmd -S $hostname -U $usr -P $pswd < $tmpfile 2>$errorfile > $resultfile

$grepcmd -q "Login failed for user" $errorfile

if [ "$?" == "0" ]; then
	$rmcmd -f $tmpfile $resultfile $errorfile;
        echo CRITICAL - Could not make connection to SQL server. Login failed.;
        exit 2;
fi

$grepcmd -q "There was a problem connecting to the server" $errorfile

if [ "$?" == "0" ]; then
        $rmcmd -f $tmpfile $resultfile $errorfile;
        echo CRITICAL - Could not make connection to SQL server. Incorrect server name or SQL service not running.;
        exit 2;
fi

resultfileln=`$catcmd $resultfile | $wccmd -l | $sedcmd 's/  //g'`

if [ "$resultfileln" == "2" ]; then
	$rmcmd -f $tmpfile $resultfile $errorfile;
        echo CRITICAL - Could not make connection to SQL server. No data received from host.;
        exit 2;
else
	nmbr=`$catcmd $resultfile | $grepcmd -v locale | $grepcmd -v charset| $grepcmd -v 1\> | $sedcmd '/^$/d' | $sedcmd 's/ //g' | $wccmd -l | sed 's/ //g'`
	users=`$catcmd $resultfile | $grepcmd -v locale | $grepcmd -v charset| $grepcmd -v 1\> | $sedcmd '/^$/d' | $sedcmd 's/ //g' | $uniqcmd -c | $trcmd \\\n , | $sedcmd 's/,$/./g' | $sedcmd 's/,/, /g' | $sedcmd 's/  //g' | $trcmd \\\t " " | $sedcmd 's/ \./\./g' | $sedcmd 's/ ,/,/g'`
        $rmcmd -f $tmpfile $resultfile;
        echo "OK - MS SQL Server $srv has $nmbr user(s) connected: $users" | sed 's/: $/./g';
        exit 0;
fi

# Cleaning up.

$rmcmd -f $tmpfile $resultfile $errorfile
echo $stdio
exit $exit
