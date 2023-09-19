#!/usr/bin/perl -w -I ..
#
# check_log tests
#
#

use strict;
use Test::More;
use NPTest;

my $tests = 18;
plan tests => $tests;

my $firstTimeOutput ='/^Log check data initialized/';
my $okOutput = '/^Log check ok - 0 pattern matches found/';
my $criticalOutput = '/^\(\d+\) < /';
my $multilineOutput = '/\(3\) <.*\n.*\n.*$/';
my $unknownOutput = '/^Usage: /';
my $unknownArgOutput = '/^Unknown argument: /';
my $bothRegexOutput = '/^Can not use extended and perl regex/';

my $result;
my $temp_file = "/tmp/check_log.tmp";
my $oldlog = "/tmp/oldlog.tmp";

open(FH, '>', $temp_file) or die $!;
close(FH);

$result = NPTest->testCmd("./check_log");
cmp_ok( $result->return_code, '==', 3, "Missing parameters" );
like  ( $result->output, $unknownOutput, "Output for unknown correct" );

$result = NPTest->testCmd("./check_log -f");
cmp_ok( $result->return_code, '==', 3, "Wrong parameters" );
like  ( $result->output, $unknownArgOutput, "Output for unknown correct" );

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'Simple match' -e -p");
cmp_ok( $result->return_code, '==', 3, "Both regex parameters" );
like  ( $result->output, $bothRegexOutput, "Output for unknown correct" );

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'Simple match'");
cmp_ok( $result->return_code, '==', 0, "First time executing" );
like  ( $result->output, $firstTimeOutput, "Output for first time executing correct" );

open(FH, '>>', $temp_file) or die $!;
print FH "This is some text, that should not match\n";
close(FH);

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'No match'");
cmp_ok( $result->return_code, '==', 0, "No match" );
like  ( $result->output, $okOutput, "Output for no match correct" );

open(FH, '>>', $temp_file) or die $!;
print FH "This text should match\n";
close(FH);

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'should match'");
cmp_ok( $result->return_code, '==', 2, "Pattern match" );
like  ( $result->output, $criticalOutput, "Output for match correct" );

open(FH, '>>', $temp_file) or die $!;
print FH "This text should not match, because it is excluded\n";
close(FH);

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'match' --exclude 'because'");
cmp_ok( $result->return_code, '==', 0, "Exclude a pattern" );
like  ( $result->output, $okOutput, "Output for no match correct" );

open(FH, '>>', $temp_file) or die $!;
print FH "Trying\nwith\nmultiline\nignore me\n";
close(FH);

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'Trying\\|with\\|multiline\\|ignore' --exclude 'me' --all");
cmp_ok( $result->return_code, '==', 2, "Multiline pattern match with --all" );
like  ( $result->output, $multilineOutput, "Output for multiline match correct" );

$result = NPTest->testCmd("./check_log -F ".$temp_file." -O ".$oldlog." -q 'match' -a");
cmp_ok( $result->return_code, '==', 0, "Non matching --all" );
like  ( $result->output, $okOutput, "Output for no match correct" );

unlink($oldlog);
unlink($temp_file);
