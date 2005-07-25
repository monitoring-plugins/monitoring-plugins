#! /usr/bin/perl -w -I ..
#
# HP JetDirect Test via check_hpjd
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 5; plan tests => $tests}

my $successOutput = '/^Printer ok - /';
my $failureOutput = '/Timeout: No [Rr]esponse from /';

my $host_tcp_hpjd      = getTestParameter( "host_tcp_hpjd",      "NP_HOST_TCP_HPJD",      undef,
					   "A host (usually a printer) providing the HP-JetDirect Services" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $t;

if ( -x "./check_hpjd" )
{
  $t += checkCmd( "./check_hpjd $host_tcp_hpjd",      0, $successOutput );
  $t += checkCmd( "./check_hpjd $host_nonresponsive", 2, $failureOutput );
  $t += checkCmd( "./check_hpjd $hostname_invalid",   3 );
}
else
{
  $t += skipMissingCmd( "./check_hpjd", $tests );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

