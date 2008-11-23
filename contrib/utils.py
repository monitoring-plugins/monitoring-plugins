#
#
# Util classes for Nagios plugins
#
#



#==========================================================================
#
# Version: = '$Id: utils.py 2 2002-02-28 06:42:51Z egalstad $'
#
# (C) Rob W.W. Hooft, Nonius BV, 1998
#
# Contact r.hooft@euromail.net for questions/suggestions.
# See: <http://starship.python.net/crew/hooft/>
# Distribute freely.
#
# jaclu@galdrion.com 2000-07-14
#   Some changes in error handling of Run() to avoid error garbage
#   when used from Nagios plugins
#   I also removed the following functions: AbortableWait() and _buttonkill() 
#   since they are only usable with Tkinter

import sys,os,signal,time,string

class error(Exception):
    pass

class _ready(Exception):
    pass

def which(filename):
    """Find the file 'filename' in the execution path. If no executable
       file is found, return None"""
    for dir in string.split(os.environ['PATH'],os.pathsep):
        fn=os.path.join(dir,filename)
        if os.path.exists(fn):
            if os.stat(fn)[0]&0111:
                return fn
    else:
        return None
    
class Task:
    """Manage asynchronous subprocess tasks.
       This differs from the 'subproc' package!
        - 'subproc' connects to the subprocess via pipes
        - 'task' lets the subprocess run autonomously.
       After starting the task, we can just:
        - ask whether it is finished yet
        - wait until it is finished
        - perform an 'idle' task (e.g. Tkinter's mainloop) while waiting for
          subprocess termination
        - kill the subprocess with a specific signal
        - ask for the exit code.
       Summarizing:
        - 'subproc' is a sophisticated os.popen()
        - 'task' is a sophisticated os.system()
       Another difference of task with 'subproc':
        - If the Task() object is deleted, before the subprocess status
          was retrieved, the child process will stay.
          It will never be waited for (i.e., the process will turn into
          a zombie. Not a good idea in general).

       Public data:
           None.

       Public methods:
           __init__, __str__, Run, Wait, Kill, Done, Status.
    """
    def __init__(self,command):
        """Constructor.
           arguments:
               command: the command to run, in the form of a string,
                        or a tuple or list of words.
           """
        if type(command)==type(''):
            self.cmd=command
            self.words=string.split(command)
        elif type(command)==type([]) or type(command)==type(()):
            # Surround each word by ' '. Limitation: words cannot contain ' chars
            self.cmd="'"+string.join(command,"' '")+"'"
            self.words=tuple(command)
        else:
            raise error("command must be tuple, list, or string")
        self.pid=None
        self.status=None

    def Run(self,usesh=0,detach=0,stdout=None,stdin=None,stderr=None):
        """Actually run the process.
           This method should be called exactly once.
           optional arguments:
               usesh=0: if 1, run 'sh -c command', if 0, split the
                        command into words, and run it by ourselves.
                        If usesh=1, the 'Kill' method might not do what
                        you want (it will kill the 'sh' process, not the
                        command).
               detach=0: if 1, run 'sh -c 'command&' (regardless of
                         'usesh'). Since the 'sh' process will immediately
                         terminate, the task created will be inherited by
                         'init', so you can safely forget it.  Remember that if
                         detach=1, Kill(), Done() and Status() will manipulate
                         the 'sh' process; there is no way to find out about the
                         detached process.
               stdout=None: filename to use as stdout for the child process.
                            If None, the stdout of the parent will be used.
               stdin= None: filename to use as stdin for the child process.
                            If None, the stdin of the parent will be used.
               stderr=None: filename to use as stderr for the child process.
                            If None, the stderr of the parent will be used.
           return value:                            
               None
        """
        if self.pid!=None:
            raise error("Second run on task forbidden")
        self.pid=os.fork()
        if not self.pid:
            for fn in range(3,256): # Close all non-standard files in a safe way
                try:
                    os.close(fn)
                except os.error:
                    pass
	    #
	    # jaclu@galdrion.com 2000-07-14
	    #
	    # I changed this bit somewhat, since Nagios plugins
	    # should send only limited errors to the caller
	    # The original setup here corupted output when there was an error.
	    # Instead the caller should check result of Wait() and anything
	    # not zero should be reported as a failure.
	    #
	    try:
		if stdout: # Replace stdout by file
		    os.close(1)
		    i=os.open(stdout,os.O_CREAT|os.O_WRONLY|os.O_TRUNC,0666)
		    if i!=1:
			sys.stderr.write("stdout not opened on 1!\n")
		if stdin: # Replace stdin by file
		    os.close(0)
		    i=os.open(stdin,os.O_RDONLY)
		    if i!=0:
			sys.stderr.write("stdin not opened on 0!\n")
		if stderr: # Replace stderr by file
		    os.close(2)
		    i=os.open(stderr,os.O_CREAT|os.O_WRONLY|os.O_TRUNC,0666)
		    if i!=2:
			sys.stdout.write("stderr not opened on 2!\n")
		#try:
                if detach:
                    os.execv('/bin/sh',('sh','-c',self.cmd+'&'))
                elif usesh:
                    os.execv('/bin/sh',('sh','-c',self.cmd))
                else:
                    os.execvp(self.words[0],self.words)
            except:
                #print self.words
                #sys.stderr.write("Subprocess '%s' execution failed!\n"%self.cmd)
                sys.exit(1)
        else:
            # Mother process
            if detach:
                # Should complete "immediately"
                self.Wait()

    def Wait(self,idlefunc=None,interval=0.1):
        """Wait for the subprocess to terminate.
           If the process has already terminated, this function will return
           immediately without raising an error.
           optional arguments:
               idlefunc=None: a callable object (function, class, bound method)
                              that will be called every 0.1 second (or see
                              the 'interval' variable) while waiting for
                              the subprocess to terminate. This can be the
                              Tkinter 'update' procedure, such that the GUI
                              doesn't die during the run. If this is set to
                              'None', the process will really wait. idlefunc
                              should ideally not take a very long time to
                              complete...
               interval=0.1: The interval (in seconds) with which the 'idlefunc'
                             (if any) will be called.
           return value:
               the exit status of the subprocess (0 if successful).
        """
        if self.status!=None:
            # Already finished
            return self.status
        if callable(idlefunc):
            while 1:
                try:
                    pid,status=os.waitpid(self.pid,os.WNOHANG)
                    if pid==self.pid:
                        self.status=status
                        return status
                    else:
                        idlefunc()
                        time.sleep(interval)
                except KeyboardInterrupt:
                    # Send the interrupt to the inferior process.
                    self.Kill(signal=signal.SIGINT)
        elif idlefunc:
            raise error("Non-callable idle function")
        else:
            while 1:
                try:
                    pid,status=os.waitpid(self.pid,0)
                    self.status=status
                    return status
                except KeyboardInterrupt:
                    # Send the interrupt to the inferior process.
                    self.Kill(signal=signal.SIGINT)

    def Kill(self,signal=signal.SIGTERM):
        """Send a signal to the running subprocess.
           optional arguments:
               signal=SIGTERM: number of the signal to send.
                               (see os.kill)
           return value:
               see os.kill()
        """
        if self.status==None:
            # Only if it is not already finished
            return os.kill(self.pid,signal)

    def Done(self):
        """Ask whether the process has already finished.
           return value:
               1: yes, the process has finished.
               0: no, the process has not finished yet.
        """
        if self.status!=None:
            return 1
        else:
            pid,status=os.waitpid(self.pid,os.WNOHANG)
            if pid==self.pid:
                #print "OK:",pid,status
                self.status=status
                return 1
            else:
                #print "NOK:",pid,status
                return 0

    def Status(self):
        """Ask for the status of the task.
           return value:
               None: process has not finished yet (maybe not even started).
               any integer: process exit status.
        """
        self.Done()
        return self.status

    def __str__(self):
        if self.pid!=None:
            if self.status!=None:
                s2="done, exit status=%d"%self.status
            else:
                s2="running"
        else:
            s2="prepared"
        return "<%s: '%s', %s>"%(self.__class__.__name__,self.cmd,s2)


#==========================================================================
#
#
# Class: TimeoutHandler
# License: GPL
# Copyright (c)  2000 Jacob Lundqvist (jaclu@galdrion.com)
#
# Version: 1.0  2000-07-14
#
# Description:
#  On init, suply a call-back kill_func that should be called on timeout
#
#  Make sure that what ever you are doing is calling Check periodically
# 
#  To check if timeout was triggered call WasTimeOut returns (true/false)
#

import time,sys

class TimeoutHandler:
    def __init__(self,kill_func,time_to_live=10,debug=0):
	'Generic time-out handler.'
	self.kill_func=kill_func
	self.start_time=time.time()
	self.stop_time=+self.start_time+int(time_to_live)
	self.debug=debug
	self.aborted=0
    
    def Check(self):
	'Call this periodically to check for time-out.'
	if self.debug:
	    sys.stdout.write('.')
	    sys.stdout.flush()
	if time.time()>=self.stop_time:
	    self.TimeOut()
	    
    def TimeOut(self):
	'Trigger the time-out callback.'
	self.aborted=1
	if self.debug:
	    print 'Timeout, aborting'
	self.kill_func()
	
    def WasTimeOut(self):
	'Indicates if timeout was triggered 1=yes, 0=no.'
	if self.debug:
	    print ''
	    print 'call duration: %.2f seconds' % (time.time()-self.start_time)
	return self.aborted
