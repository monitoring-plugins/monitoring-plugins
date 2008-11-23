#! /usr/bin/perl -w -I ..
#
# Load Average Tests via check_load
#
#

use strict;
use Test::More;
use NPTest;

my $res;

my $loadValue = "[0-9]+\.?[0-9]+";
my $successOutput = "/^OK - load average: $loadValue, $loadValue, $loadValue/";
my $failureOutput = "/^CRITICAL - load average: $loadValue, $loadValue, $loadValue/";

plan tests => 11;

$res = NPTest->testCmd( "./check_load -w 100,100,100 -c 100,100,100" );
cmp_ok( $res->return_code, 'eq', 0, "load not over 100");
like( $res->output, $successOutput, "Output OK");

$res = NPTest->testCmd( "./check_load -w 0,0,0 -c 0,0,0" );
cmp_ok( $res->return_code, 'eq', 2, "Load over 0");
like( $res->output, $failureOutput, "Output OK");

$res = NPTest->testCmd( "./check_load -r -w 0,0,0 -c 0,0,0" );
cmp_ok( $res->return_code, 'eq', 2, "Load over 0 with per cpu division");
like( $res->output, $failureOutput, "Output OK");

$res = NPTest->testCmd( "./check_load -w 100 -c 100,110" );
cmp_ok( $res->return_code, 'eq', 0, "Plugin can handle non-triplet-arguments");
like( $res->output, $successOutput, "Output OK");
like( $res->perf_output, "/load1=$loadValue;100.000;100.000/", "Test handling of non triplet thresholds (load1)");
like( $res->perf_output, "/load5=$loadValue;100.000;110.000/", "Test handling of non triplet thresholds (load5)");
like( $res->perf_output, "/load15=$loadValue;100.000;110.000/", "Test handling of non triplet thresholds (load15)");
