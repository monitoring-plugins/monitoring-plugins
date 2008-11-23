#! /usr/bin/perl -w -I ..
#
# check_nagios tests
#
#

use strict;
use Test::More;
use NPTest;

if (`uname -s` eq "SunOS\n") {
        plan skip_all => "Ignoring tests on solaris because of pst3";
} else {
        plan tests => 13;
}

my $successOutput = '/^NAGIOS OK: /';
my $warningOutput = '/^NAGIOS WARNING: /';
my $failureOutput = '/^NAGIOS CRITICAL: /';

my $nagios1 = "t/check_nagios.nagios1.status.log";
my $nagios2 = "t/check_nagios.nagios2.status.dat";

my $result;

# Did use init, but MacOSX 10.4 replaces init with launchd
# Alternative is to insist that nagios is running to run this test
# Reasonable to expect cron because build servers will 
# invoke cron to run a build
my $procname = "cron";

$result = NPTest->testCmd(
	"./check_nagios -F $nagios1 -e 5 -C $procname"
	);
cmp_ok( $result->return_code, '==', 1, "Log over 5 minutes old" );
like  ( $result->output, $warningOutput, "Output for warning correct" );

my $now = time;
# This substitution is dependant on the testcase
system( "perl -pe 's/1133537544/$now/' $nagios1 > $nagios1.tmp" ) == 0 or die "Problem with munging $nagios1";

$result = NPTest->testCmd(
	"./check_nagios -F $nagios1.tmp -e 1 -C $procname"
	);
cmp_ok( $result->return_code, "==", 0, "Log up to date" );
like  ( $result->output, $successOutput, "Output for success correct" );

my $later = $now - 61;
system( "perl -pe 's/1133537544/$later/' $nagios1 > $nagios1.tmp" ) == 0 or die "Problem with munging $nagios1";

$result = NPTest->testCmd(
        "./check_nagios -F $nagios1.tmp -e 1 -C $procname"
        );
cmp_ok( $result->return_code, "==", 1, "Log correctly seen as over 1 minute old" );
my ($age) = ($_ = $result->output) =~ /status log updated (\d+) seconds ago/;
like( $age, '/^6[0-9]$/', "Log correctly seen as between 60-69 seconds old" );

$result = NPTest->testCmd(
	"./check_nagios -F $nagios1.tmp -e 5 -C unlikely_command_string"
	);
cmp_ok( $result->return_code, "==", 2, "Nagios command not found" );
like  ( $result->output, $failureOutput, "Output for failure correct" );

$result = NPTest->testCmd(
	"./check_nagios -F $nagios2 -e 5 -C $procname"
	);
cmp_ok( $result->return_code, "==", 1, "Nagios2 for logfile over 5 mins old" );

$now = time;
system( "perl -pe 's/1133537302/$now/' $nagios2 > $nagios2.tmp" ) == 0 or die "Problem with munging $nagios2";

$result = NPTest->testCmd(
	"./check_nagios -F $nagios2.tmp -e 1 -C $procname"
	);
cmp_ok( $result->return_code, "==", 0, "Nagios2 log up to date" );

$later = $now - 61;
system( "perl -pe 's/1133537302/$later/' $nagios2 > $nagios2.tmp" ) == 0 or die "Problem with munging $nagios2";

$result = NPTest->testCmd(
        "./check_nagios -F $nagios2.tmp -e 1 -C $procname"
        );
cmp_ok( $result->return_code, "==", 1, "Nagios2 log correctly seen as over 1 minute old" );
($age) = ($_ = $result->output) =~ /status log updated (\d+) seconds ago/;
like( $age, '/^6[0-9]$/', "Log correctly seen as between 60-69 seconds old" );

$result = NPTest->testCmd(
	"./check_nagios -F t/check_nagios.t -e 1 -C $procname"
	);
cmp_ok( $result->return_code, "==", 2, "Invalid log file" );


