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

my $host_responsive    = getTestParameter("NP_HOST_RESPONSIVE", "The hostname of system responsive to network requests", "localhost");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my $t;

my $fping = qx(which fping 2> /dev/null);
chomp($fping);
if( ! -x "./check_fping") {
  $t += skipMissingCmd( "./check_fping", $tests );
}
elsif ( $> != 0 && (!$fping || ! -u $fping)) {
  $t += skipMsg( "./check_fping", $tests );
} else {
  $t += checkCmd( "./check_fping $host_responsive",    0,  '/^FPING OK - /' );
  $t += checkCmd( "./check_fping $host_nonresponsive", 2,  '/^FPING CRITICAL - /' );
  $t += checkCmd( "./check_fping $hostname_invalid",   3,  '/^FPING UNKNOWN - /' );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
