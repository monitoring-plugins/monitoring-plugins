#!/bin/sh 
################################################################################ 
# 
# CHECK_SAP plugin for Nagios 
# 
# Originally Written by Karel Salavec (karel.salavec@ct.cz) 
#
# Last Modified: 26 May 2003 by Tom De Blende (tom.deblende@village.uunet.be)
#
# Version 1.1 (Tom De Blende)
# - Added output to feed to Nagios instead of just an exit code.
# - Changed info on where to get the SAP client tools for Linux.
# 
# Version 1.0 (Karel Salavec)
#
# Command line: check_sap.sh <typ_of_check> <param1> <param2> [<param3>] 
# 
# Description: 
# This plugin will attempt to open an SAP connection with the message 
# server or application server. 
#  It need the sapinfo program installed on your server (see Notes). 
# 
#  Notes: 
#   - This plugin requires that the sapinfo program is installed. 
#   - Sapinfo is part of a client package that can be found 
#     at ftp://ftp.sap.com/pub/linuxlab/contrib/. 
# 
# 
#  Parameters: 
#  $1 - type of checking - valid values: "ms" = message server 
#                                        "as" = application server 
#  $2 - SAP server identification - can be IP address, DNS name or SAP 
#       connect string (for example: /H/saprouter/S/sapdp01/H/sapserv3) 
#  $3 - for $1="ms" - SAP system name (for example: DEV, TST, ... ) 
#       for $1="as" - SAP system number - note: central instance have sysnr=00 
#  $4 - valid only for $1="ms" - logon group name - default: PUBLIC 
# 
#  Example of command definitions for nagios: 
# 
#  command[check_sap_ms]=/usr/local/nagios/libexec/check_sap ms $HOSTADDRESS$ $ARG1$ $ARG2$ 
#  command[check_sap_as]=/usr/local/nagios/libexec/check_sap as $HOSTADDRESS$ $ARG1$ 
#  command[check_sap_ex]=/usr/local/nagios/libexec/check_sap as $ARG1$ $ARG2$ 
#                        (for ARG1 see SAP OOS1 transaction) 
#
##############################################################################

sapinfocmd='/usr/sap/rfcsdk/bin/sapinfo'
grepcmd=`which grep`
wccmd=`which wc`
cutcmd=`which cut`
awkcmd=`which awk`

##############################################################################

if [ $# -lt 3 ]; then
echo "Usage: $0 <typ_of_check> <param1> <param2> [<param3>]"
exit 2
fi

case "$1"
  in
    ms)
        if [ $4 ]
          then
            params="r3name=$3 mshost=$2 group=$4"
        else
          params="r3name=$3 mshost=$2"
        fi
        ;;
    as)
        params="ashost=$2 sysnr=$3"
        ;;
    *)
        echo "The first parameter must be ms (message server) or as (application server)!"
        exit 2
        ;;
esac

output="$($sapinfocmd $params)"
error="$(echo "$output" | $grepcmd ERROR | $wccmd -l)"
if [ "$error" -gt "0" ]; then
        output="$(echo "$output" | $grepcmd Key | $cutcmd -dy -f2)"
        echo "CRITICAL - SAP server not ready: " $output.
        exit 2
else
	output="$(echo "$output" | $grepcmd Destination | $awkcmd '{ print $2 }')"
        echo "OK - SAP server $output available."
        exit 0	
fi
