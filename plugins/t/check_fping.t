#! /usr/bin/perl -w -I ..
#
# FPing Tests via check_fping
#
#

use strict;
use Test;
use NPTest;

use vars qw($tests);

BEGIN {$tests = 4; plan tests => $tests}

my $successOutput = '/^FPING OK - /';
my $failureOutput = '/^FPING CRITICAL - /';

my $host_responsive    = getTestParameter( "host_responsive",    "NP_HOST_RESPONSIVE",    "localhost",
					   "The hostname of system responsive to network requests" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );


my $t;

if ( -x "./check_fping" )
{
  $t += checkCmd( "./check_fping $host_responsive",    0,       $successOutput );
  $t += checkCmd( "./check_fping $host_nonresponsive", [ 1, 2 ] );
  $t += checkCmd( "./check_fping $hostname_invalid",   [ 1, 2 ] );
}
else
{
  $t += skipMissingCmd( "./check_fping", $tests );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
