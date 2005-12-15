#! /usr/bin/perl -w -I ..
#
# Disk Space Tests via check_disk
#
# $Id$
#

use strict;
use Test::More;
use NPTest;
use POSIX qw(ceil floor);

my $successOutput = '/^DISK OK - /';
my $failureOutput = '/^DISK CRITICAL - /';
my $warningOutput = '/^DISK WARNING - /';

my $result;

my $mountpoint_valid  = getTestParameter( "NP_MOUNTPOINT_VALID", "Path to valid mountpoint",  "/");
my $mountpoint2_valid = getTestParameter( "NP_MOUNTPOINT2_VALID", "Path to another valid mountpoint. Must be different from 1st one", "/var");

if ($mountpoint_valid eq "" or $mountpoint2_valid eq "") {
	plan skip_all => "Need 2 mountpoints to test";
} else {
	plan tests => 26;
}

$result = NPTest->testCmd( 
	"./check_disk -w 1% -c 1% -p $mountpoint_valid -w 1% -c 1% -p $mountpoint2_valid" 
	);
cmp_ok( $result->return_code, "==", 0, "Checking two mountpoints (must have at least 1% free)");
my $c = 0;
$_ = $result->output;
$c++ while /\(/g;	# counts number of "(" - should be two
cmp_ok( $c, '==', 2, "Got two mountpoints in output");

# Calculate avg_free free on mountpoint1 and mountpoint2
# because if you check in the middle, you should get different errors
$_ = $result->output;
my ($free_on_mp1, $free_on_mp2) = (m/\((\d+)%.*\((\d+)%/);
die "Cannot parse output: $_" unless ($free_on_mp1 && $free_on_mp2);
my $avg_free = ceil(($free_on_mp1+$free_on_mp2)/2);
my ($more_free, $less_free);
if ($free_on_mp1 > $free_on_mp2) {
	$more_free = $mountpoint_valid;
	$less_free = $mountpoint2_valid;
} elsif ($free_on_mp1 < $free_on_mp2) {
	$more_free = $mountpoint2_valid;
	$less_free = $mountpoint_valid;
} else {
	die "Two mountpoints are the same - cannot do rest of test";
}


$result = NPTest->testCmd( "./check_disk -w 100 -c 100 -p $more_free" );
cmp_ok( $result->return_code, '==', 0, "At least 100 bytes available on $more_free");
like  ( $result->output, $successOutput, "OK output" );

$result = NPTest->testCmd( "./check_disk 100 100 $more_free" );
cmp_ok( $result->return_code, '==', 0, "Old syntax okay" );

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% -p $more_free" );
cmp_ok( $result->return_code, "==", 0, "At least 1% free" );

$result = NPTest->testCmd( 
	"./check_disk -w 1% -c 1% -p $more_free -w 100% -c 100% -p $less_free" 
	);
cmp_ok( $result->return_code, "==", 2, "Get critical on less_free mountpoint $less_free" );
like( $result->output, $failureOutput, "Right output" );




$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 1, "Get warning on less_free mountpoint, when checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c $avg_free% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, when checking avg_free");

$result = NPTest->testCmd( 
	"./check_disk -w $avg_free% -c 0% -p $less_free -w $avg_free% -c $avg_free% -p $more_free" 
	);
cmp_ok( $result->return_code, "==", 1, "Combining above two tests, get warning");



$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c $avg_free% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Get critical on less_free, checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $more_free -w $avg_free% -c $avg_free% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Combining above two tests, get critical");



$result = NPTest->testCmd(
	"./check_disk -w 10% -c 15% -p $mountpoint_valid"
	);
cmp_ok( $result->return_code, '==', 3, "Invalid command line options" );

TODO: {
    local $TODO = "-p must come after -w and -c";
    $result = NPTest->testCmd( 
	"./check_disk -p $mountpoint_valid -w 10% -c 15%"
	);
    cmp_ok( $result->return_code, "==", 3, "Invalid options - order unimportant" );
}

$result = NPTest->testCmd( "./check_disk -w 100% -c 100% ".${mountpoint_valid} );      # 100% empty
cmp_ok( $result->return_code, "==", 2, "100% empty" );
like( $result->output, $failureOutput, "Right output" );

TODO: {
	local $TODO = "Requesting 100GB free is should be critical";
	$result = NPTest->testCmd( "./check_disk -w 100000 -c 100000 $mountpoint_valid" );
	cmp_ok( $result->return_code, '==', 2, "Check for 100GB free" );
}

TODO: {
    local $TODO = "-u GB does not work";
    $result = NPTest->testCmd( "./check_disk -w 100 -c 100 -u GB ".${mountpoint_valid} );      # 100 GB empty
    cmp_ok( $result->return_code, "==", 2, "100 GB empty" );
}


# Checking old syntax of check_disk warn crit [fs], with warn/crit at USED% thresholds
$result = NPTest->testCmd( "./check_disk 0 0 ".${mountpoint_valid} );
cmp_ok( $result->return_code, "==", 2, "Old syntax: 0% used");

$result = NPTest->testCmd( "./check_disk 100 100 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 0, "Old syntax: 100% used" );

$result = NPTest->testCmd( "./check_disk 0 100 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 1, "Old syntax: warn 0% used" );

$result = NPTest->testCmd( "./check_disk 0 200 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );

TODO: {
	local $TODO = "Need to properly check input";
	$result = NPTest->testCmd( "./check_disk 200 200 $mountpoint_valid" );
	cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );
}

$result = NPTest->testCmd( "./check_disk 200 0 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );

