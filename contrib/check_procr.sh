#!/bin/bash

#
# Check_procr.sh 
# 
# Program: Process running check plugin for Nagios
# License : GPL
# Copyright (c) 2002 Jerome Tytgat (j.tytgat@sioban.net)
#
# check_procr.sh,v 1.0 2002/09/18 15:28 
#
# Description :
#   
#  This plugin check if at least one  process is running
#
# Usage :
#
#  check_procr.sh -p process_name
#
# Example :
#   
#  To know if snort is running
#	check_procr.sh -p snort
#	> OK - total snort running : PID=23441
#
# Linux Redhat 7.3
#

help_usage() {
        echo "Usage:"
        echo " $0 -p <process_name>"
        echo " $0 (-v | --version)"
        echo " $0 (-h | --help)"
}

help_version() {
        echo "check_procr.sh (nagios-plugins) 1.0"
        echo "The nagios plugins come with ABSOLUTELY NO WARRANTY. You may redistribute"
        echo "copies of the plugins under the terms of the GNU General Public License."
	echo "For more information about these matters, see the file named COPYING."
        echo "Copyright (c) 2002 Jerome Tytgat - j.tytgat@sioban.net"
	echo "Greetings goes to Websurg which kindly let me took time to develop this"
        echo "                  Manu Feig and Jacques Kern who were my beta testers, thanks to them !"
}

verify_dep() {
	needed="bash cut egrep expr grep let ps sed sort tail test tr wc"
	for i in `echo $needed`
	do
		type $i > /dev/null 2>&1 /dev/null
		if [ $? -eq 1 ]
		then
			echo "I am missing an important component : $i"
			echo "Cannot continue, sorry, try to find the missing one..."
			exit 3
		fi
	done
}

myself=$0

verify_dep

if [ "$1" = "-h" -o "$1" = "--help" ]
then 
	help_version	
	echo ""
	echo "This plugin will check if a process is running."
	echo ""
	help_usage
	echo ""
	echo "Required Arguments:"
        echo " -p, --process STRING"
        echo "    process name we want to verify"
	echo ""
	exit 3
fi

if [ "$1" = "-v" -o "$1" = "--version" ]
then
	help_version
        exit 3
fi

if [ `echo $@|tr "=" " "|wc -w` -lt 2 ]
then 
	echo "Bad arguments number (need two)!"
	help_usage
	exit 3
fi

tt=0
process_name=""
exclude_process_name=""
wt=""
ct=""

# Test of the command lines arguments
while test $# -gt 0
do
	
	case "$1" in
		-p|--process)
			if [ -n "$process_name" ]
			then
				echo "Only one --process argument is useful..."
                                help_usage
                                exit 3
			fi
			shift
			process_name="`echo $1|tr \",\" \"|\"`"
			;;
		*)
			echo "Unknown argument $1"
			help_usage
			exit 3
			;;
	esac
	shift
done

# ps line construction set...
for i in `ps ho pid -C $process_name`
do
	 pid_list="$pid_list $i"
done

if [ -z "$pid_list" ]
then
	crit=1
else
	crit=0
fi

# Finally Inform Nagios of what we found...
if [ $crit -eq 1 ]
then
	echo "CRITICAL - process $process_name is not running !"
	exit 2
else
	echo "OK - process $process_name is running : PID=$pid_list "
	exit 0
fi

# Hey what are we doing here ???
exit 3

