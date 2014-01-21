#! /usr/bin/perl -w -I ..
#
# TCP Connection Based Tests via check_tcp
#
#

use strict;
use Test;

use vars qw($tests $has_ipv6);
BEGIN {
    use NPTest;
    $has_ipv6 = NPTest::has_ipv6();
    $tests = $has_ipv6 ? 14 : 11;
}


my $host_tcp_http      = getTestParameter( "host_tcp_http",      "NP_HOST_TCP_HTTP",      "localhost",
					   "A host providing the HTTP Service (a web server)" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $internet_access    = getTestParameter( "NP_INTERNET_ACCESS",
                                           "Is this system directly connected to the internet?",
                                           "yes");

my $successOutput = '/^TCP OK\s-\s+[0-9]?\.?[0-9]+ second response time on port [0-9]+/';

my $failedExpect = '/^TCP WARNING\s-\sUnexpected response from host/socket on port [0-9]+/';

my $t;

$tests = $tests - 4 if $internet_access eq "no";
plan tests => $tests;

$t += checkCmd( "./check_tcp $host_tcp_http      -p 80 -wt 300 -ct 600",       0, $successOutput );
$t += checkCmd( "./check_tcp $host_tcp_http      -p 81 -wt   0 -ct   0 -to 1", 2 ); # use invalid port for this test
$t += checkCmd( "./check_tcp $host_nonresponsive -p 80 -wt   0 -ct   0 -to 1", 2 );
$t += checkCmd( "./check_tcp $hostname_invalid   -p 80 -wt   0 -ct   0 -to 1", 2 );
if($internet_access ne "no") {
    $t += checkCmd( "./check_tcp -S -D 1 -H www.verisign.com -p 443",              0 );
    $t += checkCmd( "./check_tcp -S -D 9000,1    -H www.verisign.com -p 443",      1 );
    $t += checkCmd( "./check_tcp -S -D 9000      -H www.verisign.com -p 443",      1 );
    $t += checkCmd( "./check_tcp -S -D 9000,8999 -H www.verisign.com -p 443",      2 );
}

# Need the \r\n to make it more standards compliant with web servers. Need the various quotes
# so that perl doesn't interpret the \r\n and is passed onto command line correctly
$t += checkCmd( "./check_tcp $host_tcp_http      -p 80 -E -s ".'"GET / HTTP/1.1\r\n\r\n"'." -e 'ThisShouldntMatch' -j", 1, $failedExpect );

# IPv6 checks
if($has_ipv6) {
  $t += checkCmd( "./check_tcp $host_tcp_http      -p 80 -wt 300 -ct 600 -6 ",   0, $successOutput );
  $t += checkCmd( "./check_tcp -6 -p 80 www.heise.de",                           0 );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
