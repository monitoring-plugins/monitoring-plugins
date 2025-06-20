#! /usr/bin/perl -w -I ..
#
# Ping Response Tests via check_icmp
#

use strict;
use Test::More;
use NPTest;

my $allow_sudo = getTestParameter( "NP_ALLOW_SUDO",
	"If sudo is setup for this user to run any command as root ('yes' to allow)",
	"no" );

if ($allow_sudo eq "yes" or $> == 0) {
	plan tests => 17;
} else {
	plan skip_all => "Need sudo to test check_icmp";
}
my $sudo = $> == 0 ? '' : 'sudo';

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
	"$sudo ./check_icmp -H $host_responsive -w 100ms,100% -c 100ms,100%"
	);
is( $res->return_code, 0, "Syntax ok" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -w 0ms,0% -c 100ms,100%"
	);
is( $res->return_code, 1, "Syntax ok, with forced warning" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -w 0,0% -c 0,0%"
	);
is( $res->return_code, 2, "Syntax ok, with forced critical" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_nonresponsive -w 100ms,100% -c 100ms,100%"
	);
is( $res->return_code, 2, "Timeout - host nonresponsive" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -w 100ms,100% -c 100ms,100%"
	);
is( $res->return_code, 3, "No hostname" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_nonresponsive -w 100ms,100% -c 100ms,100% -n 1 -m 0"
	);
is( $res->return_code, 0, "One host nonresponsive - zero required" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -H $host_nonresponsive -w 100ms,100% -c 100ms,100% -n 1 -m 1"
	);
is( $res->return_code, 0, "One of two host nonresponsive - one required" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -H $host_nonresponsive -w 100ms,100% -c 100ms,100% -n 1 -m 2"
	);
is( $res->return_code, 2, "One of two host nonresponsive - two required" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -s 127.0.15.15 -w 100ms,100% -c 100ms,100% -n 1"
	);
is( $res->return_code, 0, "IPv4 source_ip accepted" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -b 65507"
	);
is( $res->return_code, 0, "Try max packet size" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -R 100,100 -n 1"
	);
is( $res->return_code, 0, "rta works" );
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -P 80,90 -n 1"
	);
is( $res->return_code, 0, "pl works" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -J 80,90"
	);
is( $res->return_code, 0, "jitter works" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -M 4,3"
	);
is( $res->return_code, 0, "mos works" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -S 80,70"
	);
is( $res->return_code, 0, "score works" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -O"
	);
is( $res->return_code, 0, "order works" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -O -S 80,70 -M 4,3 -J 80,90 -P 80,90 -R 100,100"
	);
is( $res->return_code, 0, "order works" );
