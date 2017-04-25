#!/usr/bin/python

import os, sys, socket, string
from optparse import *

"""
Check_peer_status plugin for monitoring. Copyright (c) 2013 Andrea Zorzetto
This plugin is renamed to check_asterisk_peer and added support pjsip
    by Truong Ta.

Version 0.3.0 updated at 29/04/2016

This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
"""

# Define exit status
state_ok      = 0
state_warning = 1
state_critical= 2
state_unknown = 3

# Default exit status
exitcode      = 3

# Process the command line...
parser = OptionParser(usage="check_asterisk_peer [options]", version="%prog 0.3.0")
parser.set_defaults(hostname='127.0.0.1')
parser.set_defaults(port=5038)
parser.set_defaults(peer="")


parser.add_option("-u", "--username", action="store", dest="user",
        help="username for AMI.")
parser.add_option("-s", "--secret", action="store", dest="secret",
        help="password for AMI.")
parser.add_option("-H", "--host", action="store", dest="hostname",
        help="the host to connect to. The default is localhost.")
parser.add_option("-P", "--port", action="store", dest="port",
        help="the port to contact. Default is 5038.")

parser.add_option("-t", "--type", action="store", dest="type",
        help="sip or iax are allowed values.")
parser.add_option("-p", "--peer", action="store", dest="peer",
        help="the peer name to check.")
parser.add_option("-w", "--warning", action="store", dest="warning",
        help="RTT warning threshold value.")
parser.add_option("-c", "--critical", action="store", dest="critical",
        help="RTT critical threshold value.")

parser.add_option("-a", "--all", action="store_true", dest="all",
        help="print the whole output.")
parser.add_option("-v", "--verbose", action="store_true", dest="verbose",
        help="print the whole output.")

(options, args) = parser.parse_args()


# Define the socket connection
mysocket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

if (options.user) and (options.secret):
	login = """Action: login\r\nUsername: """ + options.user + """\r\nSecret: """ + options.secret + """\r\nEvents: off\r\n\r\n"""
else:
	parser.print_help()
	sys.exit(exitcode)

if (options.type=="sip"):
	command="sip show peer"
	commandall="sip show peers"
elif (options.type=="pjsip"):
	command="pjsip show aor"
	commandall="pjsip show aors"
elif (options.type=="iax"):
	command="iax2 show peer"
	commandall="iax2 show peers"
elif (options.type):
	print "Type peer error " + options.type
	sys.exit(exitcode)
else:
	print "Peer type missing"
	sys.exit(exitcode)

if (options.all):
	action = """Action: command\r\nCommand: """ + commandall + """\r\n\r\n"""
else:
	action = """Action: command\r\nCommand: """ + command + """ """+ options.peer +"""\r\n\r\n"""

logout = """Action: logoff\r\n\r\n"""



#global port
host = options.hostname
port = int(options.port)
user = options.user
password = options.secret

def connect(host, user, password):
	mysocket.connect((host, port))
	mysocket.send(login)
	
def disconnect(logout):
	send_command(logout)
	mysocket.send(logout)
	mysocket.close()
	

def send_command(action):
	mysocket.send(action)
	global myrcvd
	myrcvd = ""
	while 1:
		data = mysocket.recv(4096)    #The output bytes from the socket connection. You can adjust size to taste.
		myrcvd = myrcvd + data
		#print "$"+ data +"_"
		#print len(data)
		
		if (len(data)==0) or (string.find(data,'END COMMAND')>0):
			break
	return myrcvd

def get_peer_status(myrcvd):
	#Search peer status
	pos1= string.find(myrcvd,'Status')
	pos2= string.find(myrcvd[pos1:],'\n')

	#get peer status
	status=myrcvd[pos1:]
	status=status[:pos2]
	return status

def get_pjsip_status(myrcvd):
	# Search peer status
	pos1= string.find(myrcvd,options.peer + "/")
	pos2= string.find(myrcvd[pos1:],'\n')

	# Get peer status
	status=myrcvd[pos1:]
	status=status[:pos2]
	return status

# Perform the operation...
			
try:
	connect(host, user, password)
	result=send_command(action)
	disconnect(logout)
	
	#check auth
	if string.find(result,'accepted') != -1:
		
		if (options.all) or (options.verbose):
			print result

		elif (options.type=="pjsip"):
			status = get_pjsip_status(result)

			if (string.find(status.lower(),'unavail') >0):
				print status.split()[0].split("/")[0] + " " + status.split()[1] + "|RTT=0ms;;;"
				exitcode = state_critical
			
			elif (string.find(status.lower(),'avail') >0):
				if(options.warning) and (options.critical):
					print status.split()[0].split("/")[0] + " " + status.split()[1] + " RTT=" + status.split()[2] + "ms" \
					+ "|RTT=" + get_pjsip_status(result).split()[2] + "ms;" + options.warning + ";" + options.critical + ";0;"

					if (float (status.split()[2]) > float(options.critical)):
						exitcode = state_critical
					elif (float (status.split()[2]) > float(options.warning)):
						exitcode = state_warning
					else:
						exitcode = state_ok
	
				else:
					print status.split()[0].split("/")[0] + " " + status.split()[1] + " RTT=" + status.split()[2] + "ms" \
					+ "|RTT=" + get_pjsip_status(result).split()[2] + "ms;;;;"

					exitcode = state_ok
			
			else:
				print options.peer + " is not defined or never connected"
				exitcode = state_unknown
			
		else:
			status = get_peer_status(result)
				
			if (string.find(status,'OK') >0):
				print status
				exitcode = state_ok
			
			elif (string.find(status,'LAGGED') >0):
				print status
				exitcode = state_warning

			elif (string.find(status,'UNKNOWN') >0):
				print status
				exitcode = state_unknown

			elif (string.find(status,'unmonitored') >0):
				print status
				exitcode = state_warning

			else:
				print status
				exitcode = state_critical

	else:
		print "Critical - Authentication failed"
		exitcode = state_critical

	sys.exit(exitcode)

except socket.error:
	print "Critical - Cannot contact Asterisk!"
	sys.exit(exitcode)
