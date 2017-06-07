#! /usr/bin/perl -w -I ..
#
# Jabber Server Tests via check_jabber
#
#

use strict;
use Test::More;
use NPTest;

plan tests => 10;

my $host_tcp_jabber = getTestParameter( 
			"NP_HOST_TCP_JABBER",
			"A host providing the Jabber Service",
			"jabber.org"
			);

my $host_nonresponsive = getTestParameter(
			"NP_HOST_NONRESPONSIVE", 
			"The hostname of system not responsive to network requests",
			"10.0.0.1",
			);

my $hostname_invalid   = getTestParameter( 
			"NP_HOSTNAME_INVALID",
			"An invalid (not known to DNS) hostname",
			"nosuchhost",
			);


my $jabberOK = '/JABBER OK\s-\s\d+\.\d+\ssecond response time on '.$host_tcp_jabber.' port 5222/';

my $jabberUnresponsive = '/CRITICAL\s-\sSocket timeout after\s\d+\sseconds/';

my $jabberInvalid = '/JABBER CRITICAL - Invalid hostname, address or socket:\s.+/';

my $r;

SKIP: {
	skip "No jabber server defined", 6 unless $host_tcp_jabber;

	$r = NPTest->testCmd( "./check_jabber -H $host_tcp_jabber" );
	is( $r->return_code, 0, "Connected okay");
	like( $r->output, $jabberOK, "Output as expected" );

	$r = NPTest->testCmd( "./check_jabber -H $host_tcp_jabber -w 9 -c 9 -t 10" );
	is( $r->return_code, 0, "Connected okay, within limits" );
	like( $r->output, $jabberOK, "Output as expected" );
	
	$r = NPTest->testCmd( "./check_jabber -H $host_tcp_jabber -wt 9 -ct 9 -to 10" );
	is( $r->return_code, 0, "Old syntax okay" );
	like( $r->output, $jabberOK, "Output as expected" );

}

$r = NPTest->testCmd( "./check_jabber $host_nonresponsive" );
is( $r->return_code, 2, "Unresponsive host gives critical" );
like( $r->output, $jabberUnresponsive );

$r = NPTest->testCmd( "./check_jabber $hostname_invalid" );
is( $r->return_code, 2, "Invalid hostname gives critical" );
like( $r->output, $jabberInvalid );

