#!/bin/bash

#
# Check_procl.sh 
# 
# Program: Process load check plugin for Nagios
# License : GPL
# Copyright (c) 2002 Jerome Tytgat (j.tytgat@sioban.net)
#
# check_procl.sh,v 1.1 2002/07/04 09:35 
#
# Description :
#   
#  This plugin is for check the %cpu, %mem or cputime of one or more process
#
# Usage :
#
#  check_procl.sh -p process1,process2,... -w a.b -c c.d --cpu 
#  check_procl.sh -p process1,process2,... -w a.b -c c.d --mem
#  check_procl.sh -p process1,process2,... -w a:b:c -c d:e:f --cputime
#
#  check_procl.sh -p %all% -e process1,process2,... -w <a.b | a:b:c> -c <c.d | d:e:f> <--cpu | --mem | --cputime>
#  check_procl.sh -p %max% -e process1,process2,... -w <a.b | a:b:c> -c <c.d | d:e:f> <--cpu | --mem | --cputime>
#
# Example :
#   
#  To know the memory eaten by HTTPD processes, be warned when it reach 50% and be critical when it reach 75%
#	check_procl.sh -p httpd -w 50.0 -c 75.0 --mem
#	> OK - total %MEM for process httpd : 46.1
#
#  To know the process which eat the more cpu time, but as we are under linux and are using kapm we do :
# 	check_procl.sh -p %max% -e kapmd-idle,kapmd -w 0:1:0 -c 0:2:0 --cputime
# 	> CRITICAL - total CPUTIME for process named : 02:32:10
#
# Tested on solaris 7/8, Linux Redhat 7.3 and Linux Suse 7.1
#
# BUGS : problems with handling time on solaris...


help_usage() {
        echo "Usage:"
        echo " $0 -p <process_name1,process_name2,... | %all% | %max%>"
        echo "	  [-e <process_name1,process_name2,...>] -w warning -c critical < --cpu | --mem | --cputime>"
        echo " $0 (-v | --version)"
        echo " $0 (-h | --help)"
}

help_version() {
        echo "check_procl.sh (nagios-plugins) 1.1"
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
	echo "This plugin will check either the cumulutative %cpu, %mem or cputime"
	echo "of a process."
	echo ""
	help_usage
	echo ""
	echo "Required Arguments:"
        echo " -p, --process STRING1,STRING2,..."
        echo "    names of the processes we want to monitor,"
        echo "    you can add as much as process as you want, separated by comma,"
        echo "    hey will be cumulated"
        echo " -p, --process %all%"
        echo "    The special keyword %all% will check the cumulative cpu/mem/time of all process"
	echo "    WARNING : Can be very slow on heavy loaded servers, watch your timeout !"
        echo " -p, --process %max%"
        echo "    The special keyword %max% will check the process which eat the most"
	echo "    WARNING : only select the process which eat the more, not the cumulative,"
	echo "		    but return the cumulative"
 	echo " -w, --warning INTEGER.INTEGER or INTERGER:INTEGER:INTEGER"
	echo "    generate warning state if process count is outside this range"
	echo " -c, --critical INTEGER.INTEGER or INTERGER:INTEGER:INTEGER"
	echo "    generate critical state if process count is outside this range"
        echo " --cpu"
        echo "    return the current cpu usage for the given process"
        echo " --mem"
        echo "    return the current memory usage for the given process"
        echo " --cputime"
        echo "    return the total cputime usage for the given process"
	echo ""
        echo "Optional Argument:"
        echo " -e, --exclude-process STRING1,STRING2,..."
        echo "    names of the processes we want don't want to monitor"
        echo "    only useful when associated with %all% or %max% keywords, else ignored"
        echo "    ex : kapm-idled on linux is a process which eat memory / cputime but not really... ;-)"
	echo ""
	exit 3
fi

if [ "$1" = "-v" -o "$1" = "--version" ]
then
	help_version
        exit 3
fi

if [ `echo $@|tr "=" " "|wc -w` -lt 7 ]
then 
	echo "Bad arguments number (need at least 7)!"
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
                -e|--exclude-process)
                        if [ -n "$exclude_process_name" ]
                        then
                                echo "Only one --exclude-process argument is useful..."
                                help_usage
                                exit 3
                        fi
                        shift
                        exclude_process_name="`echo $1|tr \",\" \"|\"`"
                        ;;
                -w|--warning)
                        if [ -n "$wt" ]
                        then
                                echo "Only one --warning argument needed... Trying to test bad things ? :-)"
                                help_usage
                                exit 3
                        fi
			shift
			wt=$1
			;;
                -c|--critical)
                        if [ -n "$ct" ]
                        then
                                echo "Only one --critical argument needed... Trying to test bad things ? :-)"
                                help_usage
                                exit 3
                        fi
			shift
			ct=$1
			;;
		--cpu)
                	if [ $tt -eq 0 ]
                	then
                       		tt=1
                	else
                                echo "Only one of the arguments --cpu/--mem/--cputime can be used at a time !"
                        	help_usage
				exit 3
                	fi
			type_arg_aff="%CPU"		
			type_arg="pcpu"		
			delim="."
			;;
		--mem)
			if [ $tt -eq 0 ]
			then
                		tt=2
			else
                                echo "Only one of the arguments --cpu/--mem/--cputime can be used at a time !"
				help_usage
				exit 3
			fi
			type_arg_aff="%MEM"
			type_arg="pmem"
			delim="."
			;;
		--cputime)
                        if [ $tt -eq 0 ]
                        then
                                tt=3
                        else
                                echo "Only one of the arguments --cpu/--mem/--cputime can be used at a time !"
                                help_usage
                                exit 3
                        fi
			type_arg_aff="TIME"
			type_arg="time"
			delim=":"
			;;
		*)
			echo "Unknown argument $1"
			help_usage
			exit 3
			;;
	esac
	shift
done

# Is the process running ?
if [ -z "`ps -e | egrep \"$process_name?\"`" -a "$process_name" != "%all%" -a "$process_name" != "%max%" ]
then
	echo "WARNING: process $process_name not running !"
	exit 3
fi

# Cut of warning and critical values
wt_value1=`echo $wt|cut -d"$delim" -f1`
wt_value2=`echo $wt|cut -d"$delim" -f2`
ct_value1=`echo $ct|cut -d"$delim" -f1`
ct_value2=`echo $ct|cut -d"$delim" -f2`

if [ $tt -eq 3 ]
then
	wt_value3=`echo $wt|cut -d"$delim" -f3`
	ct_value3=`echo $ct|cut -d"$delim" -f3`
else
	wt_value3=0
	ct_value3=0
fi

# Integrity check of warning and critical values
if [ -z "$wt_value1" -o -z "$wt_value2" -o -z "$wt_value3" ]
then
        echo "Bad expression in the WARNING field : $wt"
	help_usage
        exit 3
fi

if [ "`echo $wt_value1|tr -d \"[:digit:]\"`" != "" -o "`echo $wt_value2|tr -d \"[:digit:]\"`" != "" -o "`echo $wt_value3|tr -d \"[:digit:]\"`" != "" ]
then
        echo "Bad expression in the WARNING field : $wt"
	help_usage
        exit 3
fi

if [ -z "$ct_value1" -o -z "$ct_value2" -o -z "$ct_value3" ]
then
        echo "Bad expression in the CRITICAL field : $ct"
        help_usage
        exit 3
fi


if [ "`echo $ct_value1|tr -d \"[:digit:]\"`" != "" -o "`echo $ct_value2|tr -d \"[:digit:]\"`" != "" -o "`echo $ct_value3|tr -d \"[:digit:]\"`" != "" ]
then
        echo "Bad expression in the CRITICAL field : $ct"
	help_usage
        exit 3
fi

# ps line construction set...
case "$process_name" in 
	%all%)
		if [ -z "$exclude_process_name" ]
		then
			psline=`ps -eo $type_arg,comm|egrep -v "$myself|$type_arg_aff?"|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f1`
		else
			psline=`ps -eo $type_arg,comm|egrep -v "$myself|$type_arg_aff|$exclude_process_name?"|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f1`
		fi
		;;
	%max%)
                if [ -z "$exclude_process_name" ]
                then
			pstmp=`ps -eo $type_arg,comm|egrep -v "$myself|$type_arg_aff?"|sort|tail -1|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f2`
		else
			pstmp=`ps -eo $type_arg,comm|egrep -v "$myself|$type_arg_aff|$exclude_process_name?"|sort|tail -1|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f2`
		fi
		psline=`ps -eo $type_arg,comm|grep $pstmp|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f1`
		process_name=$pstmp
		;;
	*)
		psline=`ps -eo $type_arg,comm|egrep "$process_name?"|sed "s/^ *\([0-9]\)/\1/"|cut -d" " -f1`
		;;
esac

total1=0
total2=0
total3=0


# fetching the values
for i in $psline
do
	# Special case for solaris - several format exist for the time function...
	if [ ${#i} -le 6 -a "$tt" -eq 3 ]
	then
		i="00:$i"
	fi 
	value1=`echo $i|cut -d$delim -f1`
	value2=`echo $i|cut -d$delim -f2`
	value3=`echo $i|cut -d$delim -f3`
	value3=`test -z "$value3" && echo 0 || echo $value3`
	total1=`expr $total1 + $value1`
	total2=`expr $total2 + $value2`
	total3=`expr $total3 + $value3`
	if [ $tt -eq 3 ]
	then
        	if [ $total3 -ge 60 ]
                then
                	let total2+=1
                        let total3-=60
                fi
                if [ $total2 -ge 60 ]
                then
                        let total1+=1
                        let total2-=60
                fi
	else
		if [ $total2 -ge 10 ]
		then
			let total1+=1
			let total2=total2-10
		fi
	fi
done

warn=0
crit=0

# evaluation of the cumulative values vs warning and critical values
case "$tt" in
	1)
		return_total="$total1.$total2"
		test $total1 -gt $ct_value1 && crit=1
		test $total1 -eq $ct_value1 -a $total2 -ge $ct_value2 && crit=1
		test $total1 -gt $wt_value1 && warn=1
		test $total1 -eq $wt_value1 -a $total2 -ge $wt_value2 && warn=1
		;;
	2)
		return_total="$total1.$total2"
                test $total1 -gt $ct_value1 && crit=1
                test $total1 -eq $ct_value1 -a $total2 -ge $ct_value2 && crit=1
                test $total1 -gt $wt_value1 && warn=1
                test $total1 -eq $wt_value1 -a $total2 -ge $wt_value2 && warn=1
		;;
	3)
		return_total="`test ${#total1} -eq 1 && echo 0`$total1:`test ${#total2} -eq 1 && echo 0`$total2:`test ${#total3} -eq 1 && echo 0`$total3"
                test $total1 -gt $ct_value1 && crit=1
                test $total1 -eq $ct_value1 -a $total2 -gt $ct_value2 && crit=1
                test $total1 -eq $ct_value1 -a $total2 -eq $ct_value2 -a $total3 -ge $ct_value3 && crit=1
                test $total1 -gt $wt_value1 && warn=1
                test $total1 -eq $wt_value1 -a $total2 -gt $wt_value2 && warn=1
                test $total1 -eq $wt_value1 -a $total2 -eq $wt_value2 -a $total3 -ge $wt_value3 && warn=1
		;;
esac

# last check ...
if [ $crit -eq 1 -a $warn -eq 0 ]
then
	echo "Critical value must be greater than warning value !"
	help_usage
	exit 3
fi

# Finally Inform Nagios of what we found...
if [ $crit -eq 1 ]
then
	echo "CRITICAL - total $type_arg_aff for process `echo $process_name|tr \"|\" \",\"` : $return_total"
	exit 2
elif [ $warn -eq 1 ]
then
	echo "WARNING - total $type_arg_aff for process `echo $process_name|tr \"|\" \",\"` : $return_total"
	exit 1
else
	echo "OK - total $type_arg_aff for process `echo $process_name|tr \"|\" \",\"` : $return_total"
	exit 0
fi

# Hey what are we doing here ???
exit 3