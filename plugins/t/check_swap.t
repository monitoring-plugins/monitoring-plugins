#! /usr/bin/perl -w -I ..
#
# Swap Space Tests via check_swap
#
#

use strict;
use warnings;
use Test::More tests => 14;
use NPTest;

my $successOutput = '/^OK.* - [0-9]+\% free \([0-9]+MiB out of [0-9]+MiB\)/';
my $failureOutput = '/^CRITICAL: .*- [0-9]+\% free \([0-9]+MiB out of [0-9]+MiB\)/';
my $warnOutput    = '/^WARNING: .*- [0-9]+\% free \([0-9]+MiB out of [0-9]+MiB\)/';

my $outputFormat = '--output-format one-line';
my $result;

$result = NPTest->testCmd( "./check_swap $outputFormat" );					# Always OK
cmp_ok( $result->return_code, "==", 0, "Always OK" );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -w 1048576 -c 1048576 $outputFormat" );		# 1 MB free
cmp_ok( $result->return_code, "==", 0, "At least 1MB free" );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -w 1% -c 1% $outputFormat" );			# 1% free
cmp_ok( $result->return_code, "==", 0, 'At least 1% free' );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -w 100% -c 100% $outputFormat" );			# 100% (always critical)
cmp_ok( $result->return_code, "==", 2, 'Get critical because not 100% free' );
like( $result->output, $failureOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -w 100% -c 1% $outputFormat" );			# 100% (always warn)
cmp_ok( $result->return_code, "==", 1, 'Get warning because not 100% free' );
like( $result->output, $warnOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -w 100% $outputFormat" );				# 100% (single threshold, always warn)
cmp_ok( $result->return_code, "==", 1, 'Get warning because not 100% free' );
like( $result->output, $warnOutput, "Right output" );

$result = NPTest->testCmd( "./check_swap -c 100% $outputFormat" );				# 100% (single threshold, always critical)
cmp_ok( $result->return_code, "==", 2, 'Get critical because not 100% free' );
like( $result->output, $failureOutput, "Right output" );
