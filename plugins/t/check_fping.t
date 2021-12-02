#! /usr/bin/perl -w -I ..
#
# FPing Tests via check_fping
#
#

use strict;
use Test::More;
use NPTest;

my $host_responsive    = getTestParameter("NP_HOST_RESPONSIVE", "The hostname of system responsive to network requests", "localhost");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my $res;

my $fping = qx(which fping 2> /dev/null);
chomp($fping);
if( ! -x "./check_fping") {
	plan skip_all => "check_fping not found, skipping tests";
}
elsif ( !$fping || !-x $fping ) {
	plan skip_all => "fping not found or cannot be executed, skipping tests";
} else {
  plan tests => 3;
  $res = NPTest->testCmd( "./check_fping $host_responsive" );
  cmp_ok( $res->return_code, '==', 0, "Responsive host returns OK");

  $res = NPTest->testCmd( "./check_fping $host_nonresponsive" );
  cmp_ok( $res->return_code, '==', 2, "Non-Responsive host returns Critical");

  $res = NPTest->testCmd( "./check_fping $hostname_invalid" );
  cmp_ok( $res->return_code, '==', 3, "Invalid host returns Unknown");
}
