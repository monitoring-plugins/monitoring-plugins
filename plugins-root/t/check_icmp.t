#! /usr/bin/perl -w -I ..
#
# Ping Response Tests via check_icmp
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

my $allow_sudo = getTestParameter( "NP_ALLOW_SUDO",
	"If sudo is setup for this user to run any command as root ('yes' to allow)",
	"no" );

if ($allow_sudo eq "yes") {
	plan tests => 10;
} else {
	plan skip_all => "Need sudo to test check_icmp";
}

my $successOutput = '/OK - .*?: rta [\d\.]+ms, lost \d%/';
my $failureOutput = '/(WARNING|CRITICAL) - .*?: rta [\d\.]+ms, lost \d%/';

my $host_responsive    = getTestParameter( "NP_HOST_RESPONSIVE",
				"The hostname of system responsive to network requests",
				"localhost" );

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE",
				"The hostname of system not responsive to network requests",
				"10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
                                "An invalid (not known to DNS) hostname",
				"nosuchhost" );

my $res;

$res = NPTest->testCmd(
	"sudo ./check_icmp -H $host_responsive -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 0, "Syntax ok" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"sudo ./check_icmp -H $host_responsive -w 0ms,0% -c 10000ms,100%"
	);
is( $res->return_code, 1, "Syntax ok, with forced warning" );
like( $res->output, $failureOutput, "Output OK" );

$res = NPTest->testCmd(
	"sudo ./check_icmp -H $host_responsive -w 0,0% -c 0,0%"
	);
is( $res->return_code, 2, "Syntax ok, with forced critical" );
like( $res->output, $failureOutput, "Output OK" );

$res = NPTest->testCmd(
	"sudo ./check_icmp -H $host_nonresponsive -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 2, "Timeout - host nonresponsive" );
like( $res->output, '/100%/', "Error contains '100%' string (for 100% packet loss)" );

$res = NPTest->testCmd(
	"sudo ./check_icmp -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 3, "No hostname" );
like( $res->output, '/No hosts to check/', "Output with appropriate error message");

