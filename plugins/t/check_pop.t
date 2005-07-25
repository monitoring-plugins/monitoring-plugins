#! /usr/bin/perl -w -I ..
#
# Post Office Protocol (POP) Server Tests via check_pop
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

my $host_tcp_pop       = getTestParameter( "host_tcp_pop",       "NP_HOST_TCP_POP",       $host_tcp_smtp,
					   "A host providing an POP Service (a mail server)");

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my %exceptions = ( 2 => "No POP Server present?" );

my $t;

$t += checkCmd( "./check_pop    $host_tcp_pop", 0, undef, %exceptions );
$t += checkCmd( "./check_pop -H $host_tcp_pop -p 110 -w  9 -c  9 -t  10 -e '+OK'", 0, undef, %exceptions );
$t += checkCmd( "./check_pop    $host_tcp_pop -p 110 -wt 9 -ct 9 -to 10 -e '+OK'", 0, undef, %exceptions );
$t += checkCmd( "./check_pop    $host_nonresponsive", 2 );
$t += checkCmd( "./check_pop    $hostname_invalid",   2 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
