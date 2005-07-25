#! /usr/bin/perl -w -I ..
#
# Simple Mail Transfer Protocol (SMTP) Test via check_smtp
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 5; plan tests => $tests}

my $host_tcp_smtp      = getTestParameter( "host_tcp_smtp",      "NP_HOST_TCP_SMTP",      "mailhost",
					   "A host providing an STMP Service (a mail server)");

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );
my %exceptions = ( 2 => "No SMTP Server present?" );

my $t;

$t += checkCmd( "./check_smtp    $host_tcp_smtp",                                   0, undef, %exceptions );
$t += checkCmd( "./check_smtp -H $host_tcp_smtp -p 25 -t 1 -w 9 -c 9 -t 10 -e 220", 0, undef, %exceptions );
$t += checkCmd( "./check_smtp -H $host_tcp_smtp -p 25 -wt 9 -ct 9 -to 10 -e 220",   0, undef, %exceptions );
$t += checkCmd( "./check_smtp    $host_nonresponsive", 2 );
$t += checkCmd( "./check_smtp    $hostname_invalid",   3 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
