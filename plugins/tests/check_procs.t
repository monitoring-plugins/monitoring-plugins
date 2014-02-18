#! /usr/bin/perl -w -I ..
#
# Test check_procs using input files
#

use strict;
use Test::More;
use NPTest;

if (-x "./check_procs") {
	plan tests => 50;
} else {
	plan skip_all => "No check_procs compiled";
}

my $result;
my $command = "./check_procs --input-file=tests/var/ps-axwo.darwin";

$result = NPTest->testCmd( "$command" );
is( $result->return_code, 0, "Run with no options" );
is( $result->output, "PROCS OK: 95 processes | procs=95;;;0;", "Output correct" );

$result = NPTest->testCmd( "$command -w 5" );
is( $result->return_code, 1, "Checking > 5 processes" );
is( $result->output, "PROCS WARNING: 95 processes | procs=95;5;;0;", "Output correct" );

$result = NPTest->testCmd( "$command -w 4 -c 44" );
is( $result->return_code, 2, "Checking critical" );
is( $result->output, "PROCS CRITICAL: 95 processes | procs=95;4;44;0;", "Output correct" );

$result = NPTest->testCmd( "$command -w 100 -c 200" );
is( $result->return_code, 0, "Checking no threshold breeched" );
is( $result->output, "PROCS OK: 95 processes | procs=95;100;200;0;", "Output correct" );

$result = NPTest->testCmd( "$command -C launchd -c 5" );
is( $result->return_code, 2, "Checking processes filtered by command name" );
is( $result->output, "PROCS CRITICAL: 6 processes with command name 'launchd' | procs=6;;5;0;", "Output correct" );

SKIP: {
    skip 'user with uid 501 required', 4 unless getpwuid(501);

    $result = NPTest->testCmd( "$command -u 501 -w 39 -c 41" );
    is( $result->return_code, 1, "Checking processes filtered by userid" );
    like( $result->output, '/^PROCS WARNING: 40 processes with UID = 501 (.*)$/', "Output correct" );

    $result = NPTest->testCmd( "$command -C launchd -u 501" );
    is( $result->return_code, 0, "Checking processes filtered by command name and userid" );
    like( $result->output, '/^PROCS OK: 1 process with command name \'launchd\', UID = 501 (.*)$/', "Output correct" );
}

$result = NPTest->testCmd( "$command -u -2 -w 2:2" );
is( $result->return_code, 1, "Checking processes with userid=-2" );
like( $result->output, '/^PROCS WARNING: 3 processes with UID = -2 \(nobody\)$/', "Output correct" );

$result = NPTest->testCmd( "$command -u -2 -w 3:3" );
is( $result->return_code, 0, "Checking processes with userid=-2 past threshold" );
like( $result->output, '/^PROCS OK: 3 processes with UID = -2 \(nobody\)$/', "Output correct" );

$result = NPTest->testCmd( "$command -u -2 -a usb" );
is( $result->return_code, 0, "Checking processes with userid=-2 and usb in arguments" );
like( $result->output, '/^PROCS OK: 1 process with UID = -2 \(nobody\), args \'usb\'/', "Output correct" );

$result = NPTest->testCmd( "$command -u -2 -a UsB" );
is( $result->return_code, 0, "Checking case sensitivity of args" );
like( $result->output, '/^PROCS OK: 0 processes with UID = -2 \(nobody\), args \'UsB\'/', "Output correct" );

$result = NPTest->testCmd( "$command --ereg-argument-array='mdworker.*501'" );
is( $result->return_code, 0, "Checking regexp search of arguments" );
is( $result->output, "PROCS OK: 1 process with regex args 'mdworker.*501' | procs=1;;;0;", "Output correct" );

$result = NPTest->testCmd( "$command --vsz 1000000" );
is( $result->return_code, 0, "Checking filter by VSZ" );
is( $result->output, 'PROCS OK: 24 processes with VSZ >= 1000000 | procs=24;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command --rss 100000" );
is( $result->return_code, 0, "Checking filter by RSS" );
is( $result->output, 'PROCS OK: 3 processes with RSS >= 100000 | procs=3;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command -s S" );
is( $result->return_code, 0, "Checking filter for sleeping processes" );
like( $result->output, '/^PROCS OK: 44 processes with STATE = S/', "Output correct" );

$result = NPTest->testCmd( "$command -s Z" );
is( $result->return_code, 0, "Checking filter for zombies" );
like( $result->output, '/^PROCS OK: 1 process with STATE = Z/', "Output correct" );

$result = NPTest->testCmd( "$command -p 1 -c 30" );
is( $result->return_code, 2, "Checking filter for parent id = 1" );
like( $result->output, '/^PROCS CRITICAL: 39 processes with PPID = 1/', "Output correct" );

$result = NPTest->testCmd( "$command -P 0.71" );
is( $result->return_code, 0, "Checking filter for percentage cpu > 0.71" );
is( $result->output, 'PROCS OK: 7 processes with PCPU >= 0.71 | procs=7;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command -P 0.70" );
is( $result->return_code, 0, "Checking filter for percentage cpu > 0.70" );
is( $result->output, 'PROCS OK: 8 processes with PCPU >= 0.70 | procs=8;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command --metric=CPU -w 8" );
is( $result->return_code, 1, "Checking against metric of CPU > 8" );
is( $result->output, 'CPU WARNING: 1 warn out of 95 processes | procs=95;;;0; procs_warn=1;;;0; procs_crit=0;;;0;', "Output correct" );

# TODO: Because of a conversion to int, if CPU is 1.45%, will not alert, but 2.01% will.
SKIP: {
    skip 'user with uid 501 required', 2 unless getpwuid(501);

    $result = NPTest->testCmd( "$command --metric=CPU -w 1 -u 501 -v" );
    is( $result->return_code, 1, "Checking against metric of CPU > 1 with uid=501 - TODO" );
    is( $result->output, 'CPU WARNING: 2 warn out of 40 processes with UID = 501 (tonvoon) [Skype, PubSubAgent]', "Output correct" );
};

$result = NPTest->testCmd( "$command --metric=VSZ -w 1200000 -v" );
is( $result->return_code, 1, "Checking against VSZ > 1.2GB" );
is( $result->output, 'VSZ WARNING: 4 warn out of 95 processes [WindowServer, Safari, Mail, Skype] | procs=95;;;0; procs_warn=4;;;0; procs_crit=0;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command --metric=VSZ -w 1200000 -v" );
is( $result->return_code, 1, "Checking against VSZ > 1.2GB" );
is( $result->output, 'VSZ WARNING: 4 warn out of 95 processes [WindowServer, Safari, Mail, Skype] | procs=95;;;0; procs_warn=4;;;0; procs_crit=0;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command --metric=RSS -c 70000 -v" );
is( $result->return_code, 2, "Checking against RSS > 70MB" );
is( $result->output, 'RSS CRITICAL: 5 crit, 0 warn out of 95 processes [WindowServer, SystemUIServer, Safari, Mail, Safari] | procs=95;;;0; procs_warn=0;;;0; procs_crit=5;;;0;', "Output correct" );

$result = NPTest->testCmd( "$command --ereg-argument-array='(nosuchname|nosuch2name)'" );
is( $result->return_code, 0, "Checking no pipe symbol in output" );
is( $result->output, "PROCS OK: 0 processes with regex args '(nosuchname,nosuch2name)' | procs=0;;;0;", "Output correct" );

