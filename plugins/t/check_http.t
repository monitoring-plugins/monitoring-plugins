#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_http
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 4; plan tests => $tests}

my $host_tcp_http      = getTestParameter( "host_tcp_http",      "NP_HOST_TCP_HTTP",      "localhost",
					   "A host providing the HTTP Service (a web server)" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $successOutput = '/OK.*HTTP.*second/';

my %exceptions = ( 2 => "No Web Server present?" );

my $t;

$t += checkCmd( "./check_http $host_tcp_http      -wt 300 -ct 600", { 0 => 'continue',  2 => 'skip' }, $successOutput, %exceptions );
$t += checkCmd( "./check_http $host_nonresponsive -wt   1 -ct   2", 2 );
$t += checkCmd( "./check_http $hostname_invalid   -wt   1 -ct   2", 2 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

