#! /usr/bin/perl -w -I ..
#
# Process Tests via check_procs
#
#

use strict;
use Test::More;
use NPTest;

my $t;

if (`uname -s` eq "SunOS\n" && ! -x "/usr/local/nagios/libexec/pst3") {
	plan skip_all => "Ignoring tests on solaris because of pst3";
} else {
	plan tests => 16;
}

my $result;

$result = NPTest->testCmd( "./check_procs -w 100000 -c 100000" );
is( $result->return_code, 0, "Checking less than 10000 processes" );
like( $result->output, '/^PROCS OK: [0-9]+ process(es)? | procs=[0-9]+;100000;100000;0;$/', "Output correct" );

$result = NPTest->testCmd( "./check_procs -w 100000 -c 100000 -s Z" );
is( $result->return_code, 0, "Checking less than 100000 zombie processes" );
like( $result->output, '/^PROCS OK: [0-9]+ process(es)? with /', "Output correct" );

SKIP: {
	skip "No bash available", 4 unless(system("which bash > /dev/null") == 0);
	$result = NPTest->testCmd( "bash -c './check_procs -a '/sbin/init'; true'" );
	is( $result->return_code, 0, "Parent process is ignored" );
	like( $result->output, '/^PROCS OK: 1 process?/', "Output correct" );

}
$result = NPTest->testCmd( "./check_procs -w 0 -c 100000" );
is( $result->return_code, 1, "Checking warning if processes > 0" );
like( $result->output, '/^PROCS WARNING: [0-9]+ process(es)? | procs=[0-9]+;0;100000;0;$/', "Output correct" );

$result = NPTest->testCmd( "./check_procs -w 0 -c 0" );
is( $result->return_code, 2, "Checking critical if processes > 0" );
like( $result->output, '/^PROCS CRITICAL: [0-9]+ process(es)? | procs=[0-9]+;0;0;0;$/', "Output correct" );

$result = NPTest->testCmd( "./check_procs -w 0 -c 0 -s Ss" );
is( $result->return_code, 2, "Checking critical if sleeping processes" );
like( $result->output, '/^PROCS CRITICAL: [0-9]+ process(es)? with /', "Output correct" );

$result = NPTest->testCmd( "./check_procs -w 0 -c 100000 -p 1" );
is( $result->return_code, 1, "Checking warning for processes by parentid = 1" );
like( $result->output, '/^PROCS WARNING: [0-9]+ process(es)? with PPID = 1/', "Output correct" );

