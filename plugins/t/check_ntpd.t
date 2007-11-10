#! /usr/bin/perl -w -I ..
#
# Testing NTP
#
# $Id: check_ntp.t 1468 2006-08-14 08:42:23Z tonvoon $
#

use strict;
use Test::More;
use NPTest;

plan tests => 4;

my $res;

my $ntp_service = getTestParameter( "NP_GOOD_NTP_SERVICE",
		"A host providing NTP service",
		"pool.ntp.org");

my $no_ntp_service = getTestParameter( "NP_NO_NTP_SERVICE",
		"A host NOT providing the NTP service",
		"localhost" );

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE", 
		"The hostname of system not responsive to network requests",
		"10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID", 
		"An invalid (not known to DNS) hostname",  
		"nosuchhost");

SKIP: {
	skip "No NTP server defined", 1 unless $ntp_service;
	$res = NPTest->testCmd(
		"./check_ntp -H $ntp_service"
		);
	cmp_ok( $res->return_code, '==', 0, "Got good NTP result");
}

SKIP: {
	skip "No bad NTP server defined", 1 unless $no_ntp_service;
	$res = NPTest->testCmd(
		"./check_ntp -H $no_ntp_service"
		);
	cmp_ok( $res->return_code, '==', 2, "Got bad NTP result");
}

$res = NPTest->testCmd(
	"./check_ntp -H $host_nonresponsive"
	);
cmp_ok( $res->return_code, '==', 2, "Got critical if server not responding");

$res = NPTest->testCmd(
	"./check_ntp -H $hostname_invalid"
	);
cmp_ok( $res->return_code, '==', 3, "Got critical if server hostname invalid");

