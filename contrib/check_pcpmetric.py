#! /usr/bin/env python
#
# Nagios client for checking Performance Co-Pilot metrics
#
#

from sys import argv,exit
import popen2, getopt, string, types

DEBUG=0

nagios_pcpclient_version = 0.01
PMVAL='/usr/bin/pmval'
COMMANDLINE=PMVAL + " -s 1"
METRIC='undefined'
CRITICAL=0
WARNING=0

def usage():
	print "Usage:", argv[0], "[options]"
	print "Options:"
	print "\t[-H host]\tHostname to contact"
	print "\t[-m metric]\tPCP metric to check"
	print "\t[-i instance]\tPCP metric instance"
	print "\t[-w warn]\tIssue warning alert if value is larger than this"
	print "\t[-c critical]\tIssue critical alert value is larger than this"
	print "\t[-V]\t\tProgram version"
	print "\t[-h]\t\tThis helptext"
	print ""
	print "F.ex. to check 5 minute loadaverage, warn if the load is above 2,"
	print "and give critical warning if it's above 10:"
	print "\n\t%", argv[0], " -i 5 -m kernel.all.load -w 2 -c 10"
	print ""
	print "A list of all PCP metrics can be found with the command 'pminfo'."
	print "A list of all instances within a metric can be found with 'pminfo -f metric'."
	print "F.ex. to see all available instances of 'filesys.full' execute:"
	print "\n\t% pminfo -f filesys.full"
	print "\tfilesys.full"
	print """\t\tinst [0 or "/dev/root"] value 45.35514044640914"""
	print """\t\tinst [1 or "/dev/sda1"] value 46.74285959344712"""
	print """\t\tinst [2 or "/dev/sdb1"] value 0.807766570678168"""
	print ""
	print "And the command to have nagios monitor the /dev/sda1 filesystem would be:"
	print "\n\t", argv[0], " -i /dev/sda1 -m filesys.full -w 70 -c 90"


opts, args = getopt.getopt(argv[1:],'hH:c:w:m:i:V')
for opt in opts:
	key,value = opt
	if key == '-H':
		COMMANDLINE = COMMANDLINE + " -h " + value
	elif key == '-m':
		METRIC=value
	elif key == '-i':
		COMMANDLINE = COMMANDLINE + " -i " + value
	elif key == '-c':
		CRITICAL = value
	elif key == '-w':
		WARNING = value
	elif key == '-h':
		usage()
		exit(0)
	elif key == '-V':
		print "Nagios Performance CoPilot client v%.2f" % nagios_pcpclient_version
		print "Written by Jan-Frode Myklebust <janfrode@parallab.uib.no>"
		exit(0)

if METRIC == 'undefined': 
	usage()
	exit(3)

COMMANDLINE = COMMANDLINE + " " + METRIC
if DEBUG: print COMMANDLINE
p=popen2.Popen4(COMMANDLINE)
exitcode=p.wait()

# Get the last line of output from 'pmval':
buffer = p.fromchild.readline()
while (buffer != ''):
	output=buffer
	buffer = p.fromchild.readline()
	
returndata = string.split(output)[0]


# Confirm that we have gotten a float, and not
# some errormessage in the returndata. If not, 
# print the error, and give the UNKNOWN exit code:

try:
	retval = string.atof(returndata)
except ValueError, e:
	print e
	exit(3)

if (retval < WARNING):
	EXITCODE=0
elif (retval > CRITICAL):
	EXITCODE=2
elif (retval > WARNING):
	EXITCODE=1
else:
	EXITCODE=3

print retval
exit(EXITCODE)
