#! /usr/bin/perl -w -I ..
#
# Load Average Tests via check_load
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

my $res;

my $successOutput = '/^OK - load average: [0-9]\.?[0-9]+, [0-9]\.?[0-9]+, [0-9]\.?[0-9]+/';
my $failureOutput = '/^CRITICAL - load average: [0-9]\.?[0-9]+, [0-9]\.?[0-9]+, [0-9]\.?[0-9]+/';

plan tests => 4;

$res = NPTest->testCmd( "./check_load -w 100,100,100 -c 100,100,100" );
cmp_ok( $res->return_code, 'eq', 0, "load not over 100");
like( $res->output, $successOutput, "Output OK");

$res = NPTest->testCmd( "./check_load -w 0,0,0 -c 0,0,0" );
cmp_ok( $res->return_code, 'eq', 2, "Load over 0");
like( $res->output, $failureOutput, "Output OK");

