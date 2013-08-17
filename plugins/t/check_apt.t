#!/usr/bin/perl -w -I ..
#
# Test check_apt using input files.
# Contributed by Alex Bradley, October 2012
#

use strict;
use Test::More;
use NPTest;

sub make_result_regexp {
    my ($warning, $critical) = @_;
    my $status;
    if ($warning == 0 && $critical == 0) {
	$status = "OK";
    } elsif ($critical == 0) {
	$status = "WARNING";
    } else {
	$status = "CRITICAL";
    }
    return sprintf('/^APT %s: %d packages available for upgrade \(%d critical updates\)\. |available_upgrades=%d;;;0 critical_updates=%d;;;0$/',
	$status, $warning, $critical, $warning, $critical);
}

if (-x "./check_apt") {
	plan tests => 28;
} else {
	plan skip_all => "No check_apt compiled";
}

my $result;

my $testfile_command = "./check_apt %s --input-file=t/check_apt_input/%s";

$result = NPTest->testCmd( sprintf($testfile_command, "", "debian1") );
is( $result->return_code, 0, "No upgrades" );
like( $result->output, make_result_regexp(0, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "", "debian2") );
is( $result->return_code, 1, "Debian apt output, warning" );
like( $result->output, make_result_regexp(13, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "", "debian3") );
is( $result->return_code, 2, "Debian apt output, some critical" );
like( $result->output, make_result_regexp(19, 4), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-c '^[^\\(]*\\(.* (Debian-Security:|Ubuntu:[^/]*/[^-]*-security)'", "debian3") );
is( $result->return_code, 2, "Debian apt output - should have same result when default security regexp specified via -c" );
like( $result->output, make_result_regexp(19, 4), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-i libc6", "debian3") );
is( $result->return_code, 1, "Debian apt output, filter for libc6" );
like( $result->output, make_result_regexp(3, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-i libc6 -i xen", "debian3") );
is( $result->return_code, 2, "Debian apt output, filter for libc6 and xen" );
like( $result->output, make_result_regexp(9, 4), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-i libc6 -i xen -i linux", "debian3") );
is( $result->return_code, 2, "Debian apt output, filter for libc6, xen, linux" );
like( $result->output, make_result_regexp(12, 4), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-e libc6", "debian3") );
is( $result->return_code, 2, "Debian apt output, filter out libc6" );
like( $result->output, make_result_regexp(16, 4), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-e libc6 -e xen", "debian3") );
is( $result->return_code, 1, "Debian apt output, filter out libc6 and xen" );
like( $result->output, make_result_regexp(10, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-e libc6 -e xen -e linux", "debian3") );
is( $result->return_code, 1, "Debian apt output, filter out libc6, xen, linux" );
like( $result->output, make_result_regexp(7, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-c Debian-Security -c linux", "debian3") );
is( $result->return_code, 2, "Debian apt output, critical on Debian-Security or linux" );
like( $result->output, make_result_regexp(19, 9), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "-i lib -i linux -e gc1c -c linux-image", "debian3") );
is( $result->return_code, 2, "Debian apt output, include lib and linux, exclude gc1c, critical on linux-image" );
like( $result->output, make_result_regexp(10, 2), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "", "ubuntu1") );
is( $result->return_code, 1, "Ubuntu apt output, warning" );
like( $result->output, make_result_regexp(5, 0), "Output correct" );

$result = NPTest->testCmd( sprintf($testfile_command, "", "ubuntu2") );
is( $result->return_code, 2, "Ubuntu apt output, some critical" );
like( $result->output, make_result_regexp(25, 14), "Output correct" );

