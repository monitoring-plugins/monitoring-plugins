#! /usr/bin/perl -w -I ..
#
# UDP Connection Based Tests via check_udp
#
#

use strict;
use Test::More;
use NPTest;

my $res;

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

SKIP: {
	skip "No netcat available", 6 unless (system("which nc > /dev/null") == 0);
	open (NC, "echo 'barbar' | nc -l -p 3333 -u |");
	sleep 1;
	$res = NPTest->testCmd( "./check_udp -H localhost -p 3333 -s '' -e barbar -4" );
	cmp_ok( $res->return_code, '==', 0, "Got barbar response back" );
	like  ( $res->output, '/\[barbar\]/', "Output OK");
	close NC;

	# Start up a udp server listening on port 3333, quit after 3 seconds
	# Otherwise will hang at close
	my $pid = open(NC, "nc -l -p 3333 -u -w 3 </dev/null |");
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

