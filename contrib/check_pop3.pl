#!/usr/bin/perl
# ------------------------------------------------------------------------------
# File Name:		check_pop3.pl
# Author:		Richard Mayhew - South Africa
# Date:			2000/01/21
# Version:		1.0
# Description:		This script will check to see if an POP3 is running
#			and whether authentication can take place.
# Email:		netsaint@splash.co.za
# ------------------------------------------------------------------------------
# Copyright 1999 (c) Richard Mayhew
# Credits go to Ethan Galstad for coding Nagios
# If any changes are made to this script, please mail me a copy of the
# changes :)
# License GPL
# ------------------------------------------------------------------------------
# Date		Author		Reason
# ----		------		------
# 1999/09/20	RM		Creation
# 1999/09/20	TP		Changed script to use strict, more secure by
#				specifying $ENV variables. The bind command is
#				still insecure through.  Did most of my work
#				with perl -wT and 'use strict'
# 2000/01/20	RM		Corrected POP3 Exit State.
# 2000/01/21	RM		Fix Exit Codes Again!!
# ------------------------------------------------------------------------------

# -----------------------------------------------------------------[ Require ]--
require 5.004;

# --------------------------------------------------------------------[ Uses ]--
use Socket;
use strict;

# --------------------------------------------------------------[ Enviroment ]--
$ENV{PATH} = "/bin";
$ENV{BASH_ENV} = "";
$|=1;
# ------------------------------------------------------------------[ Global ]--
my $TIMEOUT = 60;
	
# -------------------------------------------------------------------[ usage ]--
sub usage
{
	print "Minimum arguments not supplied!\n";
	print "\n";
	print "Perl Check POP3 plugin for Nagios\n";
	print "Copyright (c) 2000 Richard Mayhew\n";
	print "\n";
	print "Usage: check_pop3.pl <host> <username> <password> [port]\n";
	print "\n";
	print "<port> = Port that the pop3 daemon is running on <host>. Defaults to 110.\n";
	exit -1;

}

# --------------------------------------------------------------[ bindRemote ]--
sub bindRemote
{
	my ($in_remotehost, $in_remoteport, $in_hostname) = @_;
	my $proto;
	my $sockaddr;
	my $this;
	my $thisaddr;
	my $that;
	my ($name, $aliases,$type,$len,$thataddr) = gethostbyname($in_remotehost);

	if (!socket(ClientSocket,AF_INET, SOCK_STREAM, $proto)) { die $!; }
	$sockaddr = 'S n a4 x8';
	$this = pack($sockaddr, AF_INET, 0, $thisaddr);
	$that = pack($sockaddr, AF_INET, $in_remoteport, $thataddr);
	if (!bind(ClientSocket, $this)) { print "Connection Refused"; exit 2; }
	if (!connect(ClientSocket, $that)) { print "Connection Refused"; exit 2; }
	select(ClientSocket); $| = 1; select(STDOUT);
	return \*ClientSocket;
}

# ====================================================================[ MAIN ]==
MAIN:
{
	my $hostname;
	my $remotehost = shift || &usage;
	my $username = shift || &usage;
	my $password = shift || &usage;
	my $remoteport = shift || 110;

	# Just in case of problems, let's not hang Nagios
	$SIG{'ALRM'} = sub {
		print "Something is Taking a Long Time, Increase Your TIMEOUT (Currently Set At $TIMEOUT Seconds)\n";
		exit -1;
	};
	
	alarm($TIMEOUT);

	chop($hostname = `hostname`);
	my ($name, $alias, $proto) = getprotobyname('tcp');
	my $ClientSocket = &bindRemote($remotehost,$remoteport,$hostname);
	

print ClientSocket "user $username\n";

#Debug Server
#print "user $username\n";

#Sleep or 3 secs, incase server is slow.
sleep 3;

print ClientSocket "pass $password\n";

#Debug Server
#print "pass $password\n";

while (<ClientSocket>) {

print ClientSocket "pass $password\n";

#Debug Server
#print $_;

err($_) if (m/\-ERR\s+(.*)\s+.*/);
message($_) if (m/\+OK Mailbox open,\s+(.*\d)\s+messages.*/);
}
}

sub message 
{
	my $answer = "UNKNOWN";
  	$answer = "Pop3 OK - Total Messages On Server :- $1";	
	alarm(0);
	print ClientSocket "quit\n";
	print "$answer";
	exit 0;
}

sub err
{
	my $answer = "UNKNOWN";
	$answer = "Pop3 Error :- $1";
	alarm(0);
	print ClientSocket "quit\n";
	print "$answer";
	exit 2;
}

