#!/bin/sh 
################################################################################ 
# 
# CHECK_SAP plugin for Nagios 
# 
# Written by Karel Salavec (karel.salavec@ct.cz) 
# Last Modified: 20Apr2000 
# 
# Command line: CHECK_SAP <typ_of_check> <param1> <param2> [<param3>] 
# 
# Description: 
# This plugin will attempt to open an SAP connection with the message 
# server or application server. 
#  It need the sapinfo program installed on your server (see Notes). 
# 
#  Notes: 
#   - This plugin requires that the saprfc-devel-45A-1.i386.rpm (or higher) 
#     package be installed on your machine. Sapinfo program 
#     is a part of this package. 
#   - You can find this package at SAP ftp server in 
#    /general/misc/unsupported/linux 
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

if [ $# -lt 3 ]; then
echo "Need min. 3 parameters"
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
        echo "The first parametr must be ms (message server) or as (application server)!"
        exit 2
        ;;
esac

if /usr/sap/rfcsdk/bin/sapinfo $params | grep -i ERROR ; then
exit 2
else
exit 0
fi
