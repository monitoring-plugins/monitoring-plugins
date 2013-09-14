#! /usr/bin/perl -w -I ..
#
# Ping Response Tests via check_ping
#
#

use strict;
use Test::More;
use NPTest;

plan tests => 20;

my $successOutput = '/PING (ok|OK) - Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';
my $failureOutput = '/Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';

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
	"./check_ping -H $host_responsive -w 10,100% -c 10,100% -p 1"
	);
is( $res->return_code, 0, "Syntax ok" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_ping -H $host_responsive -w 0,0% -c 10,100% -p 1"
	);
is( $res->return_code, 1, "Syntax ok, with forced warning" );
like( $res->output, $failureOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_ping -H $host_responsive -w 0,0% -c 0,0% -p 1"
	);
is( $res->return_code, 2, "Syntax ok, with forced critical" );
like( $res->output, $failureOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_ping $host_responsive 100 100 1000 1000 -p 1"
	);
is( $res->return_code, 0, "Old syntax ok" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_ping $host_responsive 0 0 0 0 -p 1"
	);
is( $res->return_code, 2, "Old syntax, with forced critical" );
like( $res->output, $failureOutput, "Output OK" );


# check_ping results will depend on whether the ping command discovered by 
# ./configure has a timeout option. If it does, then the timeout will
# be set, so check_ping will always get a response. If it doesn't
# then check_ping will timeout. We do 2 tests for check_ping's timeout
#  - 1 second
#  - 15 seconds 
# The latter should be higher than normal ping timeouts, so should always give a packet loss result
open(F, "../config.h") or die "Cannot open ../config.h";
@_ = grep /define PING_HAS_TIMEOUT 1|define PING_PACKETS_FIRST 1/, <F>;
my $has_timeout;
$has_timeout = 1 if (scalar @_ == 2);	# Need both defined
close F;
$res = NPTest->testCmd(
	"./check_ping -H $host_nonresponsive -w 10,100% -c 10,100% -p 1 -t 1"
	);
is( $res->return_code, 2, "Timeout 1 second - host nonresponsive" );
if ($has_timeout) {
	like( $res->output, '/100%/', "Error contains '100%' string (for 100% packet loss)" );
} else {
	like( $res->output, '/timed out/', "Error contains 'timed out' string" );
}

$res = NPTest->testCmd(
	"./check_ping -H $host_nonresponsive -w 10,100% -c 10,100% -p 1 -t 15"
	);
is( $res->return_code, 2, "Timeout 15 seconds - host nonresponsive" );
like( $res->output, '/100%/', "Error contains '100%' string (for 100% packet loss)" );




$res = NPTest->testCmd(
	"./check_ping $host_nonresponsive -p 1 -t 15 100 100 1000 10000"
	);
is( $res->return_code, 2, "Old syntax: Timeout - host nonresponsive" );
like( $res->output, '/100%/', "Error contains '100%' string (for 100% packet loss)" );

$res = NPTest->testCmd(
	"./check_ping $hostname_invalid 0 0 0 0 -p 1 -t 1"
	);
is( $res->return_code, 3, "Invalid hostname" );
like( $res->output, '/invalid hostname/i', "Error contains 'invalid hostname' string");

$res = NPTest->testCmd(
	"./check_ping -w 100,10% -c 200,20%"
	);
is( $res->return_code, 3, "No hostname" );
like( $res->output, '/You must specify a server address or host name/', "Output with appropriate error message");

