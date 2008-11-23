#! /usr/bin/perl -w -I ..
#
# check_dummy tests
#
#

use strict;
use Test::More;
use NPTest;

plan tests => 20;

my $res;

$res = NPTest->testCmd("./check_dummy");
is( $res->return_code, 3, "No args" );
like( $res->output, "/Could not parse arguments/", "Correct usage message");

$res = NPTest->testCmd("./check_dummy 0");
is( $res->return_code, 0, "OK state returned");
is( $res->output, "OK", "Says 'OK'");

$res = NPTest->testCmd("./check_dummy 0 'some random data'");
is( $res->return_code, 0, "Still OK");
is( $res->output, "OK: some random data", "Sample text okay");

$res = NPTest->testCmd("./check_dummy 1");
is( $res->return_code, 1, "Warning okay");
is( $res->output, "WARNING", "Says 'WARNING'");

$res = NPTest->testCmd("./check_dummy 1 'more stuff'");
is( $res->return_code, 1, "Still warning");
is( $res->output, "WARNING: more stuff", "optional text okay" );

$res = NPTest->testCmd("./check_dummy 2");
is( $res->return_code, 2, "Critical ok" );
is( $res->output, "CRITICAL", "Says 'CRITICAL'");

$res = NPTest->testCmd("./check_dummy 2 'roughly drafted'");
is( $res->return_code, 2, "Still critical");
is( $res->output, "CRITICAL: roughly drafted", "optional text okay" );

$res = NPTest->testCmd("./check_dummy 3");
is( $res->return_code, 3, "Unknown ok" );
is( $res->output, "UNKNOWN", "Says 'UNKNOWN'");

$res = NPTest->testCmd("./check_dummy 3 'daringfireball'");
is( $res->return_code, 3, "Still unknown");
is( $res->output, "UNKNOWN: daringfireball", "optional text okay" );

$res = NPTest->testCmd("./check_dummy 4");
is( $res->return_code, 3, "Invalid error code" );
is( $res->output, "UNKNOWN: Status 4 is not a supported error state", "With appropriate error message");

