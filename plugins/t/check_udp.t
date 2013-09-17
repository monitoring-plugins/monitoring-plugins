#! /usr/bin/perl -w -I ..
#
# UDP Connection Based Tests via check_udp
#
#

use strict;
use Test::More;
use NPTest;

my $res;

alarm(120); # make sure tests don't hang

plan tests => 14;

$res = NPTest->testCmd( "./check_udp -H localhost -p 3333" );
cmp_ok( $res->return_code, '==', 3, "Need send/expect string");
like  ( $res->output, '/With UDP checks, a send/expect string must be specified./', "Output OK");

$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -s send" );
cmp_ok( $res->return_code, '==', 3, "Need expect string");
like  ( $res->output, '/With UDP checks, a send/expect string must be specified./', "Output OK");

$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -e expect" );
cmp_ok( $res->return_code, '==', 3, "Need send string");
like  ( $res->output, '/With UDP checks, a send/expect string must be specified./', "Output OK");

$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -s foo -e bar" );
cmp_ok( $res->return_code, '==', 2, "Errors correctly because no udp service running" );
like  ( $res->output, '/No data received from host/', "Output OK");

my $nc;
if(system("which nc.traditional >/dev/null 2>&1") == 0) {
	$nc = 'nc.traditional -w 3 -l -u -p 3333';
}
elsif(system("which netcat >/dev/null 2>&1") == 0) {
	$nc = 'netcat -w 3 -l -u -p 3333';
}
elsif(system("which nc >/dev/null 2>&1") == 0) {
	$nc = 'nc -w 3 -l -u -4 localhost 3333';
}

SKIP: {
	skip "solaris netcat does not listen to udp", 6 if $^O eq 'solaris';
	skip "No netcat available", 6 unless $nc;
	open (NC, "echo 'barbar' | $nc |");
	sleep 1;
	$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -s '' -e barbar -4" );
	cmp_ok( $res->return_code, '==', 0, "Got barbar response back" );
	like  ( $res->output, '/\[barbar\]/', "Output OK");
	close NC;

	# Start up a udp server listening on port 3333, quit after 3 seconds
	# Otherwise will hang at close
	my $pid = open(NC, "$nc </dev/null |");
	sleep 1;	# Allow nc to startup

	my $start = time;
	$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -s foofoo -e barbar -t 5 -4" );
	my $duration = time - $start;
	cmp_ok( $res->return_code, '==', '2', "Hung waiting for response");
	like  ( $res->output, '/Socket timeout after 5 seconds/', "Timeout message");
	like  ( $duration, '/^[56]$/', "Timeout after 5 (possibly 6) seconds");
	my $read_nc = <NC>;
	close NC;
	cmp_ok( $read_nc, 'eq', "foofoo", "Data received correctly" );
}


alarm(0); # disable alarm
