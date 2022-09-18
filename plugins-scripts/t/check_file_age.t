#!/usr/bin/perl -w -I ..
#
# check_file_age tests
#
#

use strict;
use Test::More tests => 27;
use NPTest;

my $successOutput = '/^FILE_AGE OK: /';
my $warningOutput = '/^FILE_AGE WARNING: /';
my $criticalOutput = '/^FILE_AGE CRITICAL: /';
my $unknownOutput = '/^FILE_AGE UNKNOWN: /';
my $performanceOutput = '/ \| age=[0-9]+s;[0-9:]+;[0-9:]+ size=[0-9]+B;[0-9:]+;[0-9:]+;0$/';

my $result;
my $temp_file = "/tmp/check_file_age.tmp";
my $temp_link = "/tmp/check_file_age.link.tmp";

unlink $temp_file, $temp_link;

$result = NPTest->testCmd("./check_file_age");
cmp_ok( $result->return_code, '==', 3, "Missing parameters" );
like  ( $result->output, $unknownOutput, "Output for unknown correct" );

$result = NPTest->testCmd("./check_file_age -f $temp_file");
cmp_ok( $result->return_code, '==', 2, "File not exists" );
like  ( $result->output, $criticalOutput, "Output for file missing correct" );

write_chars(100);
$result = NPTest->testCmd("./check_file_age -f $temp_file");
cmp_ok( $result->return_code, '==', 0, "File is new enough" );
like  ( $result->output, $successOutput, "Output for success correct" );

sleep 2;

$result = NPTest->testCmd("./check_file_age -f $temp_file -w 1");
cmp_ok( $result->return_code, '==', 1, "Warning for file over 1 second old" );
like  ( $result->output, $warningOutput, "Output for warning correct" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1");
cmp_ok( $result->return_code, '==', 2, "Critical for file over 1 second old" );
like  ( $result->output, $criticalOutput, "Output for critical correct" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 100");
cmp_ok( $result->return_code, '==', 0, "Checking file size" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 100");
like( $result->output, $performanceOutput, "Checking for performance Output" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 100");
like( $result->output, $performanceOutput, "Checking for performance Output from range" );

$result = NPTest->testCmd("./check_file_age -f /non/existent --ignore-missing");
cmp_ok( $result->return_code, '==', 0, "Honours --ignore-missing" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 101");
cmp_ok( $result->return_code, '==', 1, "One byte too short" );

$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -C 101");
cmp_ok( $result->return_code, '==', 2, "One byte too short - critical" );

SKIP: {
    eval 'use Monitoring::Plugin::Range';
    skip "Monitoring::Plugin::Range module require", 9 if $@;

    $result = NPTest->testCmd("./check_file_age -f $temp_file -w 0:1");
    cmp_ok( $result->return_code, '==', 1, "Warning for file over 1 second old by range" );
    like  ( $result->output, $warningOutput, "Output for warning by range correct" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 0:1");
    cmp_ok( $result->return_code, '==', 2, "Critical for file over 1 second old by range" );
    like  ( $result->output, $criticalOutput, "Output for critical by range correct" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 0:1000 -W 0:100");
    cmp_ok( $result->return_code, '==', 0, "Checking file size by range" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 101:");
    cmp_ok( $result->return_code, '==', 1, "One byte too short by range" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -W 0:99");
    cmp_ok( $result->return_code, '==', 1, "One byte too long by range" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -C 101:");
    cmp_ok( $result->return_code, '==', 2, "One byte too short by range - critical" );

    $result = NPTest->testCmd("./check_file_age -f $temp_file -c 1000 -C 0:99");
    cmp_ok( $result->return_code, '==', 2, "One byte too long by range - critical" );
};

symlink $temp_file, $temp_link or die "Cannot create symlink";
$result = NPTest->testCmd("./check_file_age -f $temp_link -c 10");
cmp_ok( $result->return_code, '==', 0, "Works for symlinks" );
unlink $temp_link;

unlink $temp_file;
mkdir $temp_file or die "Cannot create directory";
$result = NPTest->testCmd("./check_file_age -f $temp_file -c 1");
cmp_ok( $result->return_code, '==', 0, "Works for directories" );
rmdir $temp_file;


sub write_chars {
	my $size = shift;
	open F, "> $temp_file" or die "Cannot write to $temp_file";
	print F "A" x $size;
	close F;
}
