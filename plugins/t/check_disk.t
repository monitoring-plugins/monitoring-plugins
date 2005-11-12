#! /usr/bin/perl -w -I ..
#
# Disk Space Tests via check_disk
#
# $Id$
#

use strict;
use Test::More tests => 24;
use NPTest;
use POSIX qw(ceil floor);

my $successOutput = '/^DISK OK - /';
my $failureOutput = '/^DISK CRITICAL - /';
my $warningOutput = '/^DISK WARNING - /';

my $result;

my $mountpoint_valid   = getTestParameter( "mountpoint_valid",   "NP_MOUNTPOINT_VALID",   "/",
					   "The path to a valid mountpoint" );

my $mountpoint2_valid   = getTestParameter( "mountpoint2_valid",   "NP_MOUNTPOINT2_VALID",   "/var",
					   "The path to another valid mountpoint. Must be different from 1st one." );

my $free_regex    = '^DISK OK - free space: '.$mountpoint_valid.' .* MB \((\d+)%[\)]*\); '.$mountpoint2_valid.' .* MB \((\d+)%[\)]*\);|';

$result = NPTest->testCmd( "./check_disk 100 100 ".${mountpoint_valid} );              # 100 free
cmp_ok( $result->return_code, "==", 0, "At least 100 free" );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w 0 -c 0 ".${mountpoint_valid} );            # 0 free
cmp_ok( $result->return_code, "==", 0, "At least 0 free" );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% ".${mountpoint_valid} );          # 1% free
cmp_ok( $result->return_code, "==", 0, "At least 1% free" );
like( $result->output, $successOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% -p ".${mountpoint_valid}." -w 1% -c 1% -p ".$mountpoint2_valid );  # MP1 1% free MP2 100% free
cmp_ok( $result->return_code, "==", 0, "At least 1% free on mountpoint_1, 1% free on mountpoint_2" );
like( $result->output, $successOutput, "Right output" );

# Get free diskspace on NP_MOUNTPOINT_VALID and NP_MOUNTPOINT2_VALID
my $free_space_output = $result->output;
#$free_space_output =~ m/$free_regex/;
my ($free_on_mp1, $free_on_mp2) = ($free_space_output =~ m/\((\d+)%.*\((\d+)%/);
die "Cannot read free_on_mp1" unless $free_on_mp1;
die "Cannot read free_on_mp2" unless $free_on_mp2;
my $average = ceil(($free_on_mp1+$free_on_mp2)/2);
my ($larger, $smaller);
if ($free_on_mp1 > $free_on_mp2) {
	$larger = $mountpoint_valid;
	$smaller = $mountpoint2_valid;
} else {
	$larger = $mountpoint2_valid;
	$smaller = $mountpoint_valid;
}

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% -p ".${larger}." -w 100% -c 100% -p ".$smaller );  # MP1 1% free MP2 100% free
cmp_ok( $result->return_code, "==", 2, "At least 1% free on $larger, 100% free on $smaller" );
like( $result->output, $failureOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w ".$average."% -c 0% -p ".${larger}." -w ".$average."% -c ".$average."% -p ".${smaller} );          # Average free
cmp_ok( $result->return_code, "==", 2, "At least ".$average."% free on $larger" );
like( $result->output, $failureOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w ".$average."% -c ".$average."% -p ".${larger}." -w ".$average."% -c 0% -p ".${smaller} );          # Average free
cmp_ok( $result->return_code, "==", 1, "At least ".$average."% free on $smaller" );
like( $result->output, $warningOutput, "Right output" );

TODO: {
    local $TODO = "We have a bug in check_disk - -p must come after -w and -c";
    $result = NPTest->testCmd( "./check_disk -p ".${mountpoint_valid}." -w ".$average."% -c 0% -p ".${mountpoint_valid}." -w ".$average."% -c ".$average."%" );          # Average free
    cmp_ok( $result->return_code, "==", 2, "At least ".$average."% free on mountpoint_1" );
    like( $result->output, $failureOutput, "Right output" );

    $result = NPTest->testCmd( "./check_disk -p ".${mountpoint_valid}." -w ".$average."% -c ".$average."% -p ".${mountpoint_valid}." -w ".$average."% -c 0%" );          # Average free
    cmp_ok( $result->return_code, "==", 1, "At least ".$average."% free on mountpoint_2" );
    like( $result->output, $warningOutput, "Right output" );
}

$result = NPTest->testCmd( "./check_disk -w 100% -c 100% ".${mountpoint_valid} );      # 100% empty
cmp_ok( $result->return_code, "==", 2, "100% empty" );
like( $result->output, $failureOutput, "Right output" );

TODO: {
    local $TODO = "-u GB sometimes does not work?";
    $result = NPTest->testCmd( "./check_disk -w 100 -c 100 -u GB ".${mountpoint_valid} );      # 100 GB empty
    cmp_ok( $result->return_code, "==", 2, "100 GB empty" );
    like( $result->output, $failureOutput, "Right output" );
}

$result = NPTest->testCmd( "./check_disk 0 0 ".${mountpoint_valid} );                  # 0 critical
cmp_ok( $result->return_code, "==", 2, "No empty space" );
like( $result->output, $failureOutput, "Right output" );
