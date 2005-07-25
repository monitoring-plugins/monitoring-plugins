#! /usr/bin/perl -w -I ..
#
# UDP Connection Based Tests via check_udp
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 3; plan tests => $tests} #TODO# Update to 4 when the commented out test is fixed

my $host_udp_time      = getTestParameter( "host_udp_time",      "NP_HOST_UDP_TIME",      "localhost",
                                           "A host providing the UDP Time Service" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
                                           "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $successOutput = '/^Connection accepted on port [0-9]+ - [0-9]+ second response time$/';

my $t;

$t += checkCmd( "./check_udp -H $host_udp_time      -p 37 -wt 300 -ct 600",       0, $successOutput );
$t += checkCmd( "./check_udp    $host_nonresponsive -p 37 -wt 0   -ct   0 -to 1", 2 );
#TODO# $t += checkCmd( "./check_udp    $hostname_invalid   -p 37 -wt 0   -ct   0 -to 1", 2 ); # Currently returns 0 (ie success)

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
