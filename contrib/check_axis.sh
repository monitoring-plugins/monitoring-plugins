#!/bin/sh

box=$1
port=$2
usr=$3
pass=$4

if [ ! "$#" == "4" ]; then
	echo -e "\nYou did not supply enough command line arguments. \nUsage: ./check_axis.sh <host> <port> <username> <password> \n \nCheck_axis.sh checks the status of LPT ports on Axis print servers. \nIt was written by Tom De Blende (tom.deblende@village.uunet.be) in 2002. \n" && exit "3"
fi

tempfile=/tmp/status-$box.tmp
exit="3"

ftp -in $box &>/dev/null <<EOF
user $usr $pass
passive
prompt off
lcd /tmp
ascii
get status $tempfile
EOF

if [ ! -e "$tempfile" ]; then
        stdio="Status file could not be transferred from the Axis box." && rm -f $tempfile && echo $stdio && exit 2;
fi

lines=`cat $tempfile | grep -i $port`
status=`echo $lines | awk '{ print $3 }'`
if [ "$status" == "Printing" ]; then
	bytes=`echo $lines | awk '{ print $4 }'`;
	comments=`echo $lines | tr -d "
" | awk '{ print $5 " " $6 }'`;
else
	comments=`echo $lines | tr -d "
" | awk '{ print $4 " " $5 }'`;
fi

comma=`echo $comments | grep , | wc -l`
if [ "$comma" -eq "1" ]; then
	comments=`echo $comments | cut -d, -f1`
fi
	

if [ "$status" == "Available" ]; then
	if [ "$comments" == "Paper out" ]; then
		exit="1" && stdio="WARNING - Out of paper.";
        elif [ "$comments" == " " ]; then
                exit="0" && stdio="OK - Printer is available but returns no comments.";
	elif [ "$comments" == "No error" ]; then
		exit="0" && stdio="OK - No error.";
        elif [ "$comments" == "Ready " ]; then
                exit="0" && stdio="OK - Ready.";
        elif [ "$comments" == "Off line" ]; then
                exit="1" && stdio="WARNING - Printer is off line.";        
        elif [ "$comments" == "Out of" ]; then
                exit="1" && stdio="WARNING - Out of paper.";	
	elif [ "$comments" == "Busy Out" ]; then
                exit="1" && stdio="WARNING - Busy, out of paper.";
        elif [ "$comments" == "Printer off-line" ]; then
                exit="1" && stdio="WARNING - Printer is off line.";	
        elif [ "$comments" == "Printer fault" ]; then
                exit="2" && stdio="CRITICAL - Printer fault.";
	else
		exit="3" && stdio="Comments: $comments";
	fi
elif [ "$status" == "Printing" ]; then
	if [ "$comments" == "Printer busy" ]; then
		exit="0" && stdio="OK - PRINTING. Bytes printed: $bytes.";
        elif [ "$comments" == "No error" ]; then
                exit="0" && stdio="OK - PRINTING. Bytes printed: $bytes.";        
	elif [ "$comments" == "Paper out" ]; then
                exit="1" && stdio="WARNING - PRINTING. Out of paper.";
        elif [ "$comments" == "Out of" ]; then
                exit="1" && stdio="WARNING - PRINTING. Out of paper. Bytes printed: $bytes.";        
        elif [ "$comments" == "Busy Out" ]; then
                exit="1" && stdio="WARNING - Busy, out of paper.";
	elif [ "$comments" == "Ready " ]; then
                exit="0" && stdio="OK - PRINTING. Bytes printed: $bytes.";        
        elif [ "$comments" == "Printer off-line" ]; then
                exit="1" && stdio="WARNING - PRINTING. Printer is off line.";
        elif [ "$comments" == "Busy " ]; then
                exit="0" && stdio="OK - PRINTING. Busy. Bytes printed: $bytes.";	
	elif [ "$comments" == "Off line" ]; then
                exit="1" && stdio="WARNING - PRINTING. Printer is off line.";
        elif [ "$comments" == "Printer fault" ]; then
                exit="2" && stdio="CRITICAL - PRINTING. Printer fault. Bytes printed: $bytes.";        
	else
                exit="3" && stdio="Comments: $comments.";
	fi
fi

rm -f $tempfile
echo $stdio
exit $exit
