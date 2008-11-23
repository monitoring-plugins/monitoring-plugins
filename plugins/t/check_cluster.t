#! /usr/bin/perl -w -I ..
#
# check_cluster tests
#
#

use strict;
use Test::More tests => 15;
use NPTest;

my $result;

$result = NPTest->testCmd(
	"./check_cluster -s -w 0:0 -c 0:0 -d 0,0,0,0"
	);
cmp_ok( $result->return_code, '==', 0, "Exit OK if non-ok services are inside critical and warning ranges" );
like( $result->output, qr/service/i, "Output contains the word 'service' (case insensitive)");

$result = NPTest->testCmd(
	"./check_cluster -l LABEL -s -w 0:0 -c 0:0 -d 0,0,0,0"
	);
like( $result->output, qr/LABEL/, "Output contains the defined label 'LABEL' (case sensitive)");

$result = NPTest->testCmd(
	"./check_cluster -s -w 0:0 -c 0:1 -d 0,0,0,1"
	);
cmp_ok( $result->return_code, '==', 1, "Exit WARNING if non-ok services are inside critical and outside warning ranges" );

$result = NPTest->testCmd(
	"./check_cluster -s -w 0:0 -c 0:1 -d 0,0,1,1"
	);
cmp_ok( $result->return_code, '==', 2, "Exit CRITICAL if non-ok services are inside critical and outside warning ranges" );

$result = NPTest->testCmd(
	"./check_cluster -s -w 0 -c 0 -d 0,0,0,0"
	);
cmp_ok( $result->return_code, '==', 0, "Exit OK if non-ok services are inside critical and warning (no ranges)" );

$result = NPTest->testCmd(
	"./check_cluster -s -w 0 -c 1 -d 0,0,1,0"
	);
cmp_ok( $result->return_code, '==', 1, "Exit WARNING if number of non-ok services exceed warning (no ranges)" );

$result = NPTest->testCmd(
	"./check_cluster -s -w 0 -c 1 -d 0,0,1,1"
	);
cmp_ok( $result->return_code, '==', 2, "Exit Critical if non-ok services exceed critical warning (no ranges)" );


#
# And for hosts..
#
$result = NPTest->testCmd(
	"./check_cluster -h -w 0:0 -c 0:0 -d 0,0,0,0"
	);
cmp_ok( $result->return_code, '==', 0, "Exit OK if non-ok hosts are inside critical and warning ranges" );
like( $result->output, qr/host/i, "Output contains the word 'host' (case insensitive)");

$result = NPTest->testCmd(
	"./check_cluster -h -w 0:0 -c 0:1 -d 0,0,0,1"
	);
cmp_ok( $result->return_code, '==', 1, "Exit WARNING if non-ok hosts are inside critical and outside warning ranges" );

$result = NPTest->testCmd(
	"./check_cluster -h -w 0:0 -c 0:1 -d 0,0,1,1"
	);
cmp_ok( $result->return_code, '==', 2, "Exit CRITICAL if non-ok hosts are inside critical and outside warning ranges" );

$result = NPTest->testCmd(
	"./check_cluster -h -w 0 -c 0 -d 0,0,0,0"
	);
cmp_ok( $result->return_code, '==', 0, "Exit OK if non-ok hosts are inside critical and warning (no ranges)" );

$result = NPTest->testCmd(
	"./check_cluster -h -w 0 -c 1 -d 0,0,1,0"
	);
cmp_ok( $result->return_code, '==', 1, "Exit WARNING if number of non-ok hosts exceed warning (no ranges)" );

$result = NPTest->testCmd(
	"./check_cluster -h -w 0 -c 1 -d 0,0,1,1"
	);
cmp_ok( $result->return_code, '==', 2, "Exit Critical if non-ok hosts exceed critical warning (no ranges)" );
