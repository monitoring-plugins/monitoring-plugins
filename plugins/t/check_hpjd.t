#! /usr/bin/perl -w -I ..
#
# HP JetDirect Test via check_hpjd
#
#

use strict;
use Test::More;
use NPTest;

plan skip_all => "check_hpjd not compiled" unless (-x "check_hpjd");

plan tests => 5;

my $successOutput = '/^Printer ok - /';
my $failureOutput = '/Timeout: No [Rr]esponse from /';

my $host_tcp_hpjd = getTestParameter( 
			"NP_HOST_TCP_HPJD",
			"A host (usually a printer) providing the HP-JetDirect Services"
			);

my $host_nonresponsive = getTestParameter( 
			"NP_HOST_NONRESPONSIVE",
			"The hostname of system not responsive to network requests",
			"10.0.0.1",
			);

my $hostname_invalid = getTestParameter( 
			"NP_HOSTNAME_INVALID",
			"An invalid (not known to DNS) hostname",
			"nosuchhost",
			);

my $res;

SKIP: {
	skip "No HP JetDirect defined", 2 unless $host_tcp_hpjd;
	$res = NPTest->testCmd("./check_hpjd $host_tcp_hpjd");
	cmp_ok( $res->return_code, '==', 0, "Jetdirect responding" );
	like  ( $res->output, $successOutput, "Output correct" );
}

$res = NPTest->testCmd("./check_hpjd $host_nonresponsive");
cmp_ok( $res->return_code, 'eq', 2, "Host not responding");
like  ( $res->output, $failureOutput, "Output OK" );

$res = NPTest->testCmd("./check_hpjd $hostname_invalid");
cmp_ok( $res->return_code, 'eq', 3, "Hostname invalid");

