#!@PERL@ -w

# -----------------------------------------------------------------------------
# File Name:		check_ircd.pl
#
# Author:		Richard Mayhew - South Africa
#
# Date:			1999/09/20
#
#
# Description:		This script will check to see if an IRCD is running
#			about how many users it has
#
# Email:		netsaint@splash.co.za
#
# -----------------------------------------------------------------------------
# Copyright 1999 (c) Richard Mayhew
#
# If any changes are made to this script, please mail me a copy of the
# changes :)
#
# Some code taken from Charlie Cook (check_disk.pl)
#
# License GPL
#
# -----------------------------------------------------------------------------
# Date		Author		Reason
# ----		------		------
#
# 1999/09/20	RM		Creation
#
# 1999/09/20	TP		Changed script to use strict, more secure by
#				specifying $ENV variables. The bind command is
#				still insecure through.  Did most of my work
#				with perl -wT and 'use strict'
#
# test using check_ircd.pl (irc-2.mit.edu|irc.erols.com|irc.core.com)
# 2002/05/02    SG		Fixed for Embedded Perl
#

# ----------------------------------------------------------------[ Require ]--

require 5.004;

# -------------------------------------------------------------------[ Uses ]--

use Socket;
use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_t $opt_p $opt_H $opt_w $opt_c $verbose);
use vars qw($PROGNAME);
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support &usage);

# ----------------------------------------------------[ Function Prototypes ]--

sub print_help ();
sub print_usage ();
sub connection ($$$$);
sub bindRemote ($$);

# -------------------------------------------------------------[ Enviroment ]--

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

# -----------------------------------------------------------------[ Global ]--

$PROGNAME = "check_ircd";
my $NICK="ircd$$";
my $USER_INFO="monitor localhost localhost : ";
	
# -------------------------------------------------------------[ connection ]--
sub connection ($$$$)
{
	my ($in_remotehost,$in_users,$in_warn,$in_crit) = @_;
	my $state;
	my $answer;

	print "connection(debug): users = $in_users\n" if $verbose;
	$in_users =~ s/\ //g;
	
	if ($in_users >= 0) {

		if ($in_users > $in_crit) {
			$state = "CRITICAL";
			$answer = "Critical Number Of Clients Connected : $in_users (Limit = $in_crit)\n";

		} elsif ($in_users > $in_warn) {
			$state = "WARNING";
			$answer = "Warning Number Of Clients Connected : $in_users (Limit = $in_warn)\n";

		} else {
			$state = "OK";
			$answer = "IRCD ok - Current Local Users: $in_users\n";
		}

	} else {
		$state = "UNKNOWN";
		$answer = "Server $in_remotehost has less than 0 users! Something is Really WRONG!\n";
	}
	
	print ClientSocket "quit\n";
	print $answer;
	exit $ERRORS{$state};
}

# ------------------------------------------------------------[ print_usage ]--

sub print_usage () {
	print "Usage: $PROGNAME -H <host> [-w <warn>] [-c <crit>] [-p <port>]\n";
}

# -------------------------------------------------------------[ print_help ]--

sub print_help ()
{
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2000 Richard Mayhew/Karl DeBisschop

Perl Check IRCD plugin for monitoring

";
	print_usage();
	print "
-H, --hostname=HOST
   Name or IP address of host to check
-w, --warning=INTEGER
   Number of connected users which generates a warning state (Default: 50)
-c, --critical=INTEGER
   Number of connected users which generates a critical state (Default: 100)
-p, --port=INTEGER
   Port that the ircd daemon is running on <host> (Default: 6667)
-v, --verbose
   Print extra debugging information
";
}

# -------------------------------------------------------------[ bindRemote ]--

sub bindRemote ($$)
{
	my ($in_remotehost, $in_remoteport) = @_;
	my $proto = getprotobyname('tcp');
	my $sockaddr;
	my $that;
	my ($name, $aliases,$type,$len,$thataddr) = gethostbyname($in_remotehost);

	if (!socket(ClientSocket,AF_INET, SOCK_STREAM, $proto)) {
	    print "IRCD UNKNOWN: Could not start socket ($!)\n";
	    exit $ERRORS{"UNKNOWN"};
	}
	$sockaddr = 'S n a4 x8';
	$that = pack($sockaddr, AF_INET, $in_remoteport, $thataddr);
	if (!connect(ClientSocket, $that)) { 
	    print "IRCD UNKNOWN: Could not connect socket ($!)\n";
	    exit $ERRORS{"UNKNOWN"};
	}
	select(ClientSocket); $| = 1; select(STDOUT);
	return \*ClientSocket;
}

# ===================================================================[ MAIN ]==

MAIN:
{
	my $hostname;

	Getopt::Long::Configure('bundling');
	GetOptions
	 ("V"   => \$opt_V,  "version"    => \$opt_V,
		"h"   => \$opt_h,  "help"       => \$opt_h,
		"v"   => \$verbose,"verbose"    => \$verbose,
		"t=i" => \$opt_t,  "timeout=i"  => \$opt_t,
		"w=i" => \$opt_w,  "warning=i"  => \$opt_w,
		"c=i" => \$opt_c,  "critical=i" => \$opt_c,
		"p=i" => \$opt_p,  "port=i"     => \$opt_p,
		"H=s" => \$opt_H,  "hostname=s" => \$opt_H);

	if ($opt_V) {
		print_revision($PROGNAME,'@NP_VERSION@');
		exit $ERRORS{'UNKNOWN'};
	}

	if ($opt_h) {print_help(); exit $ERRORS{'UNKNOWN'};}

	($opt_H) || ($opt_H = shift @ARGV) || usage("Host name/address not specified\n");
	my $remotehost = $1 if ($opt_H =~ /([-.A-Za-z0-9]+)/);
	($remotehost) || usage("Invalid host: $opt_H\n");

	($opt_w) || ($opt_w = shift @ARGV) || ($opt_w = 50);
	my $warn = $1 if ($opt_w =~ /^([0-9]+)$/);
	($warn) || usage("Invalid warning threshold: $opt_w\n");

	($opt_c) || ($opt_c = shift @ARGV) || ($opt_c = 100);
	my $crit = $1 if ($opt_c =~ /^([0-9]+)$/);
	($crit) || usage("Invalid critical threshold: $opt_c\n");

	($opt_p) || ($opt_p = shift @ARGV) || ($opt_p = 6667);
	my $remoteport = $1 if ($opt_p =~ /^([0-9]+)$/);
	($remoteport) || usage("Invalid port: $opt_p\n");

	if ($opt_t && $opt_t =~ /^([0-9]+)$/) { $TIMEOUT = $1; }

	# Just in case of problems, let's not hang the monitoring system
	$SIG{'ALRM'} = sub {
		print "Somthing is Taking a Long Time, Increase Your TIMEOUT (Currently Set At $TIMEOUT Seconds)\n";
		exit $ERRORS{"UNKNOWN"};
	};
	
	alarm($TIMEOUT);

	my ($name, $alias, $proto) = getprotobyname('tcp');

	print "MAIN(debug): binding to remote host: $remotehost -> $remoteport\n" if $verbose;
	my $ClientSocket = &bindRemote($remotehost,$remoteport);
	
	print ClientSocket "NICK $NICK\nUSER $USER_INFO\n";
	
	while (<ClientSocket>) {
		print "MAIN(debug): default var = $_\n" if $verbose;

		# DALnet,LagNet,UnderNet etc. Require this!
		# Replies with a PONG when presented with a PING query.
		# If a server doesn't require it, it will be ignored.
	
		if (m/^PING (.*)/) {print ClientSocket "PONG $1\n";}
	
		alarm(0);
	
		# Look for pattern in IRCD Output to gather Client Connections total.
		connection($remotehost,$1,$warn,$crit) if (m/:I have\s+(\d+)/);
	}
	print "IRCD UNKNOWN: Unknown error - maybe could not authenticate\n";
	exit $ERRORS{"UNKNOWN"};
}
