#! /usr/bin/perl -w -I ..
#
# Swap Space Tests via check_swap
#
#

use strict;
use warnings;
use Test::More tests => 21;
use NPTest;
use JSON;

my $result;
my $outputFormat = '--output-format mp-test-json';
my $output;
my $message = '/^[0-9]+\% free \([0-9]+MiB out of [0-9]+MiB\)/';

$result = NPTest->testCmd( "./check_swap $outputFormat" );					# Always OK
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "OK", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -w 1048576 -c 1048576 $outputFormat" );		# 1 MB free
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "OK", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -w 1% -c 1% $outputFormat" );			# 1% free
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "OK", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -w 100% -c 100% $outputFormat" );			# 100% (always critical)
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "CRITICAL", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -w 100% -c 1% $outputFormat" );			# 100% (always warn)
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "WARNING", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -w 100% $outputFormat" );				# 100% (single threshold, always warn)
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "WARNING", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");

$result = NPTest->testCmd( "./check_swap -c 100% $outputFormat" );				# 100% (single threshold, always critical)
cmp_ok( $result->return_code, "==", 0, "Always OK" );
is($result->{'mp_test_result'}->{'state'}, "CRITICAL", "State was correct");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $message, "Output was correct");
