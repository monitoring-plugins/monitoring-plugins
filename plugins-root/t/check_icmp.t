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
<<<<<<< HEAD
	plan tests => 18;
=======
	plan tests => 20;
>>>>>>> 44f19e79 (Fix tests (hopefully))
} else {
	plan skip_all => "Need sudo to test check_icmp";
}
my $sudo = $> == 0 ? '' : 'sudo';

my $successOutput = '/OK - .*?: rta (?:[\d\.]+ms)|(?:nan), lost \d+%/';
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

# 1
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 0, "Syntax ok" );
like( $res->output, $successOutput, "Output OK" );

# 2
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -w 0ms,0% -c 10000ms,100%"
	);
is( $res->return_code, 1, "Syntax ok, with forced warning" );
like( $res->output, $failureOutput, "Output OK" );

# 3
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -w 0,0% -c 0,0%"
	);
is( $res->return_code, 2, "Syntax ok, with forced critical" );
like( $res->output, $failureOutput, "Output OK" );

# 4
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_nonresponsive -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 2, "Timeout - host nonresponsive" );
like( $res->output, '/100%/', "Error contains '100%' string (for 100% packet loss)" );

# 4
$res = NPTest->testCmd(
	"$sudo ./check_icmp -w 10000ms,100% -c 10000ms,100%"
	);
is( $res->return_code, 3, "No hostname" );
like( $res->output, '/No hosts to check/', "Output with appropriate error message");

# 5
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_nonresponsive -w 10000ms,100% -c 10000ms,100% -n 1 -m 0"
	);
is( $res->return_code, 0, "One host nonresponsive - zero required" );
like( $res->output, $successOutput, "Output OK" );

# 6
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -H $host_nonresponsive -w 10000ms,100% -c 10000ms,100% -n 1 -m 1"
	);
is( $res->return_code, 0, "One of two host nonresponsive - one required" );
like( $res->output, $successOutput, "Output OK" );

# 7
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -H $host_nonresponsive -w 10000ms,100% -c 10000ms,100% -n 1 -m 2"
	);
is( $res->return_code, 2, "One of two host nonresponsive - two required" );
like( $res->output, $failureOutput, "Output OK" );

# 8
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -s 127.0.15.15 -w 10000ms,100% -c 10000ms,100% -n 1 -m 2"
	);
is( $res->return_code, 0, "IPv4 source_ip accepted" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"$sudo ./check_icmp -H $host_responsive -b 65507"
	);
is( $res->return_code, 0, "Try max paket size" );
like( $res->output, $successOutput, "Output OK - Didn't overflow" );

# 9
$res = NPTest->testCmd(
	"$sudo ./check_icmp -H ::1 -vvv"
	);
is( $res->return_code, 0, "Try IPv6 and vvv" );
like( $res->output, $successOutput, "Output OK - Didn't overflow" );
