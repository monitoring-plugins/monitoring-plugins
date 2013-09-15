#! /usr/bin/perl -w -I ..
#
# DHCP Tests via check_dhcp
#

use strict;
use Test::More;
use NPTest;

my $allow_sudo = getTestParameter( "NP_ALLOW_SUDO",
	"If sudo is setup for this user to run any command as root ('yes' to allow)",
	"no" );

if ($allow_sudo eq "yes" or $> == 0) {
	plan tests => 4;
} else {
	plan skip_all => "Need sudo to test check_dhcp";
}
my $sudo = $> == 0 ? '' : 'sudo';

my $successOutput = '/OK: Received \d+ DHCPOFFER\(s\), \d+ of 1 requested servers responded, max lease time = \d+ sec\./';
my $failureOutput = '/CRITICAL: Received \d+ DHCPOFFER\(s\), 0 of \d+ requested servers responded/';

my $host_responsive    = getTestParameter( "NP_HOST_DHCP_RESPONSIVE",
				"The hostname of system responsive to dhcp requests",
				"localhost" );

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE",
				"The hostname of system not responsive to dhcp requests",
				"10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
                                "An invalid (not known to DNS) hostname",
				"nosuchhost" );

my $res;

$res = NPTest->testCmd(
	"$sudo ./check_dhcp -s $host_responsive"
	);
is( $res->return_code, 0, "Syntax ok" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"$sudo ./check_dhcp -s $host_nonresponsive"
	);
is( $res->return_code, 2, "Timeout - host nonresponsive" );
like( $res->output, $failureOutput, "Output OK" );

