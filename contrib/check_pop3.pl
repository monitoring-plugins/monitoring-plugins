#!/usr/bin/perl
# ------------------------------------------------------------------------------
# File Name:    check_pop3.pl
# Author:    Richard Mayhew - South Africa
# Date:      2000/01/21
# Version:    1.0
# Description:    This script will check to see if an POP3 is running
#      and whether authentication can take place.
# Email:    netsaint@splash.co.za
# ------------------------------------------------------------------------------
# Copyright 1999 (c) Richard Mayhew
# Credits go to Ethan Galstad for coding Nagios
# If any changes are made to this script, please mail me a copy of the
# changes :)
# License GPL
# ------------------------------------------------------------------------------
# Date    Author    Reason
# ----    ------    ------
# 1999/09/20  RM    Creation
# 1999/09/20  TP    Changed script to use strict, more secure by
#        specifying $ENV variables. The bind command is
#        still insecure through.  Did most of my work
#        with perl -wT and 'use strict'
# 2000/01/20  RM    Corrected POP3 Exit State.
# 2000/01/21  RM    Fix Exit Codes Again!!
# 2003/12/30  CZ    Proper CRLF in communication w/server
#        Fixed infinite loop
#        Error checking on welcome banner, USER, PASS commands
#        Better error condition handling

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
	if (!bind(ClientSocket, $this)) { print "Connection Refused\n"; exit 2; }
	if (!connect(ClientSocket, $that)) { print "Connection Refused\n"; exit 2; }
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
	

	&err("no welcome banner\n") unless $_ = <ClientSocket>;
	&err("bad welcome banner: " . $_) unless $_ =~ /^\+OK/;

	print ClientSocket "USER $username\r\n";

	&err("no response to USER command\n") unless $_ = <ClientSocket>;
	&err("bad response to USER: " . $_) unless $_ =~ /^\+OK/;

	print ClientSocket "PASS $password\r\n";

	&err("no response to PASS command\n") unless $_ = <ClientSocket>;
	&err("bad response to PASS: " . $_) unless $_ =~ /^\+OK/;

	print ClientSocket "LIST\r\n";

	my $bad = 1;
	my $msgs = 0;
	while (<ClientSocket>) {
		&err(($1||' UNKNOWN')."\n") if (m/\-ERR(.*)/);
		$bad = 0 if /^\+OK/;
		$msgs = $1 if /^(\d+)\s+/;
		last if /^\./;
	}
	&message("$msgs\n") unless $bad;
	&err("missing +OK to LIST command\n");
}

sub message 
{
	my $msg = shift;
	alarm(0);
	print ClientSocket "QUIT\r\n";
	print "POP3 OK - Total Messages On Server: $msg";
	exit 0;
}

sub err
{
	my $msg = shift;
	alarm(0);
	print ClientSocket "QUIT\r\n";
	print "POP3 Error: $msg";
	exit 2;
}
