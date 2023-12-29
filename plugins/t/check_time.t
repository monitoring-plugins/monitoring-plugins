#! /usr/bin/perl -w -I ..
#
# System Time Tests via check_time
#
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 8; plan tests => $tests}

my $host_udp_time      = getTestParameter("NP_HOST_UDP_TIME", "A host providing the UDP Time Service", "localhost");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my $successOutput = '/^TIME OK - [0-9]+ second time difference/';

my %exceptions = ( 3 => "No time server present?");

my $t;

# standard mode
$t += checkCmd( "./check_time -H $host_udp_time -w 999999,59       -c 999999,59       -t 60", 0, $successOutput, %exceptions );
$t += checkCmd( "./check_time -H $host_udp_time -w 999999    -W 59 -c 999999    -C 59 -t 60", 0, $successOutput, %exceptions );

# reverse compatibility mode
$t += checkCmd( "./check_time    $host_udp_time -wt 59 -ct 59 -cd 999999 -wd 999999 -to 60",  0, $successOutput, %exceptions );

# failure mode
$t += checkCmd( "./check_time -H $host_nonresponsive -t 1", 2 );
$t += checkCmd( "./check_time -H $hostname_invalid   -t 1", 3 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
