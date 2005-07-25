#! /usr/bin/perl -w -I ..
#
# Ping Response Tests via check_ping
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);

BEGIN {$tests = 6; plan tests => $tests}

my $successOutput = '/PING (ok|OK) - Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';
my $failureOutput = '/Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';

my $host_responsive    = getTestParameter( "host_responsive",   "NP_HOST_RESPONSIVE",     "localhost",
					   "The hostname of system responsive to network requests" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $t;

$t += checkCmd( "./check_ping $host_responsive    100 100 1000 1000 -p 1",       0, $successOutput );
$t += checkCmd( "./check_ping $host_responsive      0   0    0    0 -p 1",       2, $failureOutput );
$t += checkCmd( "./check_ping $host_nonresponsive   0   0    0    0 -p 1 -to 1", 2 );
$t += checkCmd( "./check_ping $hostname_invalid     0   0    0    0 -p 1 -to 1", 3 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
