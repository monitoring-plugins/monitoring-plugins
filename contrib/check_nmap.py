#!/usr/bin/python
# Change the above line if python is somewhere else

#
# check_nmap
#  
# Program: nmap plugin for Nagios
# License: GPL
# Copyright (c)  2000 Jacob Lundqvist (jaclu@galdrion.com)
#
_version_ = '1.21'
#
#
# Description:
#
# Does a nmap scan, compares open ports to those given on command-line
# Reports warning for closed that should be open and error for
# open that should be closed.
# If optional ports are given, no warning is given if they are closed
# and they are included in the list of valid ports.
#
#   Requirements:
#     python
#     nmap
#
# History
# -------
# 1.21	 2004-07-23 rippeld@hillsboroughcounty.org Updated parsing of nmap output to correctly identify closed ports
# 1.20   2000-07-15 jaclu Updated params to correctly comply to plugin-standard
#                         moved support classes to utils.py
# 1.16   2000-07-14 jaclu made options and return codes more compatible with 
#                         the plugin developer-guidelines
# 1.15   2000-07-14 jaclu added random string to temp-file name
# 1.14   2000-07-14 jaclu added check for error from subproc
# 1.10   2000-07-14 jaclu converted main part to class
# 1.08   2000-07-13 jaclu better param parsing
# 1.07   2000-07-13 jaclu changed nmap param to -P0
# 1.06   2000-07-13 jaclu make sure tmp file is deleted on errors
# 1.05   2000-07-12 jaclu in debug mode, show exit code
# 1.03   2000-07-12 jaclu error handling on nmap output
# 1.01   2000-07-12 jaclu added license
# 1.00   2000-07-12 jaclu implemented timeout handling
# 0.20   2000-07-10 jaclu Initial release


import sys, os, string, whrandom

import tempfile
from getopt import getopt

#
# import generic Nagios-plugin stuff
#
import utils

# Where temp files should be placed
tempfile.tempdir='/usr/local/nagios/var'

# Base name for tempfile
tempfile.template='check_nmap_tmp.'

# location and possibly params for nmap
nmap_cmd='/usr/bin/nmap -P0'






#
#  the class that does all the real work in this plugin...
#
# 
class CheckNmap:

    # Retcodes, so we are compatible with nagios
    #ERROR=    -1
    UNKNOWN=  -1
    OK=        0
    WARNING=   1
    CRITICAL=  2


    def __init__(self,cmd_line=[]):
        """Constructor.
           arguments:
	     cmd_line: normaly sys.argv[1:] if called as standalone program
	"""
	self.tmp_file=''
	self.host=''       # host to check
	self.timeout=10    
	self.debug=0       # 1= show debug info
	self.ports=[]      # list of mandatory ports
	self.opt_ports=[]  # list of optional ports
	self.ranges=''     # port ranges for nmap
	self.exit_code=0   # numerical exit-code
	self.exit_msg=''   # message to caller
	
	self.ParseCmdLine(cmd_line)
	
    def Run(self):
        """Actually run the process.
           This method should be called exactly once.
	"""
	
	#
	# Only call check_host if cmd line was accepted earlier
	#
	if self.exit_code==0:
	    self.CheckHost()

	self.CleanUp()
	return self.exit_code,self.exit_msg
    
    def Version(self):
	return 'check_nmap %s' % _version_
    
    #-----------------------------------------
    #
    # class internal stuff below...
    #
    #-----------------------------------------
    
    #
    # Param checks
    #
    def param2int_list(self,s):
	lst=string.split(string.replace(s,',',' '))
	try:
	    for i in range(len(lst)):
		lst[i]=int(lst[i])
	except:
	    lst=[]
	return lst
	    
    def ParseCmdLine(self,cmd_line):
	try:
	    opt_list=getopt(cmd_line,'vH:ho:p:r:t:V',['debug','host=','help',
	        'optional=','port=','range=','timeout','version'])
	    for opt in opt_list[0]:
		if opt[0]=='-v' or opt[0]=='--debug':
		    self.debug=1
		elif opt[0]=='-H' or opt[0]=='--host':
		    self.host=opt[1]
		elif opt[0]=='-h' or opt[0]=='--help':
		    doc_help()
		    self.exit_code=1 # request termination
		    break
		elif opt[0]=='-o' or opt[0]=='--optional':
		    self.opt_ports=self.param2int_list(opt[1])
		elif opt[0]=='-p' or opt[0]=='--port':
		    self.ports=self.param2int_list(opt[1])
		elif opt[0]=='-r' or opt[0]=='--range':
		    r=string.replace(opt[1],':','-')
		    self.ranges=r
		elif opt[0]=='-t' or opt[0]=='--timeout':
		    self.timeout=opt[1]
		elif opt[0]=='-V' or opt[0]=='--version':
		    print self.Version()
		    self.exit_code=1 # request termination
		    break
		else:
		    self.host=''
		    break

	except:
	    # unknown param
	    self.host=''
	    
	if self.debug:
	    print 'Params:'
	    print '-------'
	    print 'host             = %s' % self.host
	    print 'timeout          = %s' % self.timeout
	    print 'ports            = %s' % self.ports
	    print 'optional ports   = %s' % self.opt_ports
	    print 'ranges           = %s' % self.ranges
	    print
	
	#
	# a option that wishes us to terminate now has been given...
	# 
	# This way, you can test params in debug mode and see what this 
	# program recognised by suplying a version param at the end of
	# the cmd-line
	#
	if self.exit_code<>0:
	    sys.exit(self.UNKNOWN)
	    
	if self.host=='':
	    doc_syntax()
	    self.exit_code=self.UNKNOWN
	    self.exit_msg='UNKNOWN: bad params, try running without any params for syntax'
	       	

    def CheckHost(self):
	'Check one host using nmap.'
	#
	# Create a tmp file for storing nmap output
	#
	# The tempfile module from python 1.5.2 is stupid
	# two processes runing at aprox the same time gets 
	# the same tempfile...
	# For this reason I use a random suffix for the tmp-file
	# Still not 100% safe, but reduces the risk significally
	# I also inserted checks at various places, so that
	# _if_ two processes in deed get the same tmp-file
	# the only result is a normal error message to nagios
	#
	r=whrandom.whrandom()
	self.tmp_file=tempfile.mktemp('.%s')%r.randint(0,100000)
	if self.debug:
	    print 'Tmpfile is: %s'%self.tmp_file
	#
	# If a range is given, only run nmap on this range
	#
	if self.ranges<>'':
	    global nmap_cmd # needed, to avoid error on next line
	                    # since we assigns to nmap_cmd :)
	    nmap_cmd='%s -p %s' %(nmap_cmd,self.ranges)	
	#
	# Prepare a task
	#
	t=utils.Task('%s %s' %(nmap_cmd,self.host))
	#
	# Configure a time-out handler
	#
	th=utils.TimeoutHandler(t.Kill, time_to_live=self.timeout, 
	                        debug=self.debug)
	#
	#  Fork of nmap cmd
	#
	t.Run(detach=0, stdout=self.tmp_file,stderr='/dev/null')
	#
	# Wait for completition, error or timeout
	#
	nmap_exit_code=t.Wait(idlefunc=th.Check, interval=1)
	#
	# Check for timeout
	#
	if th.WasTimeOut():
	    self.exit_code=self.CRITICAL
	    self.exit_msg='CRITICAL - Plugin timed out after %s seconds' % self.timeout
	    return
	#
	# Check for exit status of subprocess
	# Must do this after check for timeout, since the subprocess
	# also returns error if aborted.
	#
	if nmap_exit_code <> 0:
	    self.exit_code=self.UNKNOWN
	    self.exit_msg='nmap program failed with code %s' % nmap_exit_code
	    return
	#
	# Read output
	#
	try:
	    f = open(self.tmp_file, 'r')
	    output=f.readlines()
	    f.close()
	except:
	    self.exit_code=self.UNKNOWN
            self.exit_msg='Unable to get output from nmap'
	    return

	#
	# Store open ports in list
	#  scans for lines where first word contains '/'
	#  and stores part before '/'
	#
	self.active_ports=[]
	try:
	    for l in output:
		if len(l)<2:
		    continue
		s=string.split(l)[0]
		if string.find(s,'/')<1:
		    continue
		p=string.split(s,'/')[0]
		if string.find(l,'open')>1:
		    self.active_ports.append(int(p))
	except:
	    # failure due to strange output...
	    pass

	if self.debug:
	    print 'Ports found by nmap:   ',self.active_ports
	#
	# Filter out optional ports, we don't check status for them...
	#
	try:
	    for p in self.opt_ports:
		self.active_ports.remove(p)
	    
	    if self.debug and len(self.opt_ports)>0:
		print 'optional ports removed:',self.active_ports
	except:
	    # under extreame loads the remove(p) above failed for me
	    # a few times, this exception hanlder handles
	    # this bug-alike situation...
	    pass

	opened=self.CheckOpen()	
	closed=self.CheckClosed()
	
	if opened <>'':
	    self.exit_code=self.CRITICAL
            self.exit_msg='PORTS CRITICAL - Open:%s Closed:%s'%(opened,closed)
	elif closed <>'':
	    self.exit_code=self.WARNING
	    self.exit_msg='PORTS WARNING - Closed:%s'%closed
	else:
	    self.exit_code=self.OK
	    self.exit_msg='PORTS ok - Only defined ports open'
    
    
    #
    # Compares requested ports on with actually open ports
    # returns all open that should be closed
    #
    def CheckOpen(self):
	opened=''
	for p in self.active_ports:
	    if p not in self.ports:
		opened='%s %s' %(opened,p)
	return opened
	
    #
    # Compares requested ports with actually open ports
    # returns all ports that are should be open
    #
    def CheckClosed(self):
	closed=''
	for p in self.ports:
	    if p not in self.active_ports:
		closed='%s %s' % (closed,p)
	return closed


    def CleanUp(self):
	#
	# If temp file exists, get rid of it
	#
	if self.tmp_file<>'' and os.path.isfile(self.tmp_file):
	    try:
		os.remove(self.tmp_file)
	    except:
		# temp-file colition, some other process already
		# removed the same file... 
		pass	    
    
	#
	# Show numerical exits as string in debug mode
	#
	if self.debug:
	    print 'Exitcode:',self.exit_code,
	    if self.exit_code==self.UNKNOWN:
		print 'UNKNOWN'
	    elif self.exit_code==self.OK:
		print 'OK'
	    elif self.exit_code==self.WARNING:
		print 'WARNING'
	    elif self.exit_code==self.CRITICAL:
		print 'CRITICAL'
	    else:
		print 'undefined'
	#
	# Check if invalid exit code
	#
	if self.exit_code<-1 or self.exit_code>2:
	    self.exit_msg=self.exit_msg+' - undefined exit code (%s)' % self.exit_code
	    self.exit_code=self.UNKNOWN


        


#
# Help texts
#
def doc_head():
    print """
check_nmap plugin for Nagios
Copyright (c) 2000 Jacob Lundqvist (jaclu@galdrion.com)
License: GPL
Version: %s""" % _version_
    

def doc_syntax():
    print """
Usage: check_ports [-v|--debug] [-H|--host host] [-V|--version] [-h|--help]
                   [-o|--optional port1,port2,port3 ...] [-r|--range range]
                   [-p|--port port1,port2,port3 ...] [-t|--timeout timeout]"""
    

def doc_help():
    'Help is displayed if run without params.'
    doc_head()
    doc_syntax()
    print """
Options:
 -h         = help (this screen ;-)
 -v         = debug mode, show some extra output
 -H host    = host to check (name or IP#)
 -o ports   = optional ports that can be open (one or more),
	      no warning is given if optional port is closed
 -p ports   = ports that should be open (one or more)
 -r range   = port range to feed to nmap.  Example: :1024,2049,3000:7000
 -t timeout = timeout in seconds, default 10
 -V         = Version info
 
This plugin attempts to verify open ports on the specified host.

If all specified ports are open, OK is returned.
If any of them are closed, WARNING is returned (except for optional ports)
If other ports are open, CRITICAL is returned

If possible, supply an IP address for the host address, 
as this will bypass the DNS lookup.        
"""


#
# Main
#
if __name__ == '__main__':

    if len (sys.argv) < 2:
	#
	# No params given, show syntax and exit
	#
	doc_syntax()
	sys.exit(-1)
	
    nmap=CheckNmap(sys.argv[1:])
    exit_code,exit_msg=nmap.Run()
    
    #
    # Give Nagios a msg and a code
    #
    print exit_msg
    sys.exit(exit_code)
