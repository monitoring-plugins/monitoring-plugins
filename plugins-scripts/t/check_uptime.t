#!/usr/bin/perl -w -I ..
#
# check_uptime tests
#
#

use strict;
use Test::More tests => 40;
use NPTest;

my $result;

$result = NPTest->testCmd(
	"./check_uptime"
	);
cmp_ok( $result->return_code, '==', 3, "Missing parameters" );
like  ( $result->output, '/^Usage: check_uptime -w/', "Output for missing parameters correct" );

$result = NPTest->testCmd(
	"./check_uptime --help"
	);
cmp_ok( $result->return_code, '==', 3, "Help output requested" );
like  ( $result->output, '/ABSOLUTELY NO WARRANTY/', "Output for help correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 5 -c 2"
	);
cmp_ok( $result->return_code, '==', 3, "Warning greater than critical" );
like  ( $result->output, '/^Upper Warning .*cannot be greater than Critical/', "Output for warning greater than critical correct" );

$result = NPTest->testCmd(
	"./check_uptime -c 1000 -W 100 2>&1"
	);
like  ( $result->output, '/^Unknown option: W/', "Output with wrong parameter is correct" );

$result = NPTest->testCmd(
	"./check_uptime -f -w 1 -c 2"
	);
cmp_ok( $result->return_code, '==', 2, "Uptime higher than 2 seconds" );
like  ( $result->output, '/Running for \d+/', "Output for the f parameter correct" );

$result = NPTest->testCmd(
	"./check_uptime -s -w 1 -c 2"
	);
cmp_ok( $result->return_code, '==', 2, "Uptime higher than 2 seconds" );
like  ( $result->output, '/Running since \d+/', "Output for the s parameter correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 1 -c 2"
	);
cmp_ok( $result->return_code, '==', 2, "Uptime higher than 2 seconds" );
like  ( $result->output, '/^CRITICAL: uptime is \d+ seconds/', "Output for uptime higher than 2 seconds correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 1 -c 9999w"
	);
cmp_ok( $result->return_code, '==', 1, "Uptime lower than 9999 weeks" );
like  ( $result->output, '/^WARNING: uptime is \d+ seconds/', "Output for uptime lower than 9999 weeks correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 9998w -c 9999w"
	);
cmp_ok( $result->return_code, '==', 0, "Uptime lower than 9998 weeks" );
like  ( $result->output, '/^OK: uptime is \d+ seconds/', "Output for uptime lower than 9998 weeks correct" );
like  ( $result->output, '/\|uptime=[0-9]+s;6046790400;6047395200;/', "Checking for performance output" );

$result = NPTest->testCmd(
	"./check_uptime -w 111222d -c 222333d"
	);
cmp_ok( $result->return_code, '==', 0, "Uptime lower than 111222 days" );
like  ( $result->output, '/^OK: uptime is \d+ seconds/', "Output for uptime lower than 111222 days correct" );
like  ( $result->output, '/\|uptime=[0-9]+s;9609580800;19209571200;/', "Checking for performance output" );

# Same as before, hopefully uptime is higher than 2 seconds so no warning
$result = NPTest->testCmd(
	"./check_uptime -w 2:111222d -c 1:222333d"
	);
cmp_ok( $result->return_code, '==', 0, "Uptime lower than 111222 days, and higher 2 seconds" );
like  ( $result->output, '/^OK: uptime is \d+ seconds/', "Output for uptime lower than 111222 days, and higher 2 seconds correct" );
like  ( $result->output, '/\|uptime=[0-9]+s;9609580800;19209571200;/', "Checking for performance output" );

# Same as before, now the low warning should trigger
$result = NPTest->testCmd(
	"./check_uptime -w 111221d:111222d -c 1:222333d"
	);
cmp_ok( $result->return_code, '==', 1, "Uptime lower than 111221 days raises warning" );
like  ( $result->output, '/^WARNING: uptime is \d+ seconds/', "Output for uptime lower than 111221 days correct" );
like  ( $result->output, '/Exceeds lower warn threshold/', "Exceeds text correct" );
like  ( $result->output, '/\|uptime=[0-9]+s;9609580800;19209571200;/', "Checking for performance output" );

# Same as before, now the low critical should trigger
$result = NPTest->testCmd(
	"./check_uptime -w 111221d:111222d -c 111220d:222333d"
	);
cmp_ok( $result->return_code, '==', 2, "Uptime lower than 111220 days raises critical" );
like  ( $result->output, '/^CRITICAL: uptime is \d+ seconds/', "Output for uptime lower than 111220 days correct" );
like  ( $result->output, '/Exceeds lower crit threshold/', "Exceeds text correct" );
like  ( $result->output, '/\|uptime=[0-9]+s;9609580800;19209571200;/', "Checking for performance output" );


#
# Range values using ":" without two parts ("a:b") is invalid
# Strings without two parts are always considered as upper threshold
#

$result = NPTest->testCmd(
	"./check_uptime -w 2: -c 1:4"
	);
cmp_ok( $result->return_code, '==', 3, "Wrong parameter format raises unknown" );
like  ( $result->output, '/^Upper warning .* is not numeric/', "Output for wrong parameter format correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 2:3 -c 1:"
	);
cmp_ok( $result->return_code, '==', 3, "Wrong parameter format raises unknown" );
like  ( $result->output, '/^Upper critical .* is not numeric/', "Output for wrong parameter format correct" );

$result = NPTest->testCmd(
	"./check_uptime -w :3 -c 1:4"
	);
cmp_ok( $result->return_code, '==', 3, "Wrong parameter format raises unknown" );
like  ( $result->output, '/^Upper warning .* is not numeric/', "Output for wrong parameter format correct" );

$result = NPTest->testCmd(
	"./check_uptime -w 2:3 -c :4"
	);
cmp_ok( $result->return_code, '==', 3, "Wrong parameter format raises unknown" );
like  ( $result->output, '/^Upper critical .* is not numeric/', "Output for wrong parameter format correct" );

