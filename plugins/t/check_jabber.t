#! /usr/bin/perl -w -I ..
#
# Jabber Server Tests via check_jabber
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 10; plan tests => $tests}

my $host_tcp_jabber = getTestParameter( 
			"NP_HOST_TCP_JABBER",
			"A host providing the Jabber Service",
			"jabber.org"
			);

my $host_nonresponsive = getTestParameter(
			"NP_HOST_NONRESPONSIVE", 
			"The hostname of system not responsive to network requests",
			"10.0.0.1",
			);

my $hostname_invalid   = getTestParameter( 
			"NP_HOSTNAME_INVALID",
			"An invalid (not known to DNS) hostname",
			"nosuchhost",
			);

my %exceptions = ( 2 => "No Jabber Server present?" );

my $jabberOK = '/JABBER OK\s-\s\d+\.\d+\ssecond response time on port 5222/';

my $jabberUnresponsive = '/CRITICAL\s-\sSocket timeout after\s\d+\sseconds/';

my $jabberInvalid = '/check_JABBER: Invalid hostname, address or socket\s-\s.+/';

my $t;

$t += checkCmd( "./check_jabber $host_tcp_jabber", 0, $jabberOK );

$t += checkCmd( "./check_jabber -H $host_tcp_jabber -w 9 -c 9 -t 10", 0, $jabberOK );

$t += checkCmd( "./check_jabber $host_tcp_jabber -wt 9 -ct 9 -to 10", 0, $jabberOK );

$t += checkCmd( "./check_jabber $host_nonresponsive", 2, $jabberUnresponsive );

$t += checkCmd( "./check_jabber $hostname_invalid", 2, $jabberInvalid );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

