#! /usr/bin/perl -w -I ..
#
# Testing NTP
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

my @PLUGINS1 = ('check_ntp', 'check_ntp_peer', 'check_ntp_time');
my @PLUGINS2 = ('check_ntp_peer');

plan tests => (9 * scalar(@PLUGINS1)) + (6 * scalar(@PLUGINS2));

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

my $hostname_invalid = getTestParameter( "NP_HOSTNAME_INVALID", 
		"An invalid (not known to DNS) hostname",  
		"nosuchhost");

my $ntp_okmatch1 = '/^NTP\sOK:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs/';
my $ntp_warnmatch1 = '/^NTP\sWARNING:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs/';
my $ntp_critmatch1 = '/^NTP\sCRITICAL:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs/';
my $ntp_okmatch2 = '/^NTP\sOK:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2}/';
my $ntp_warnmatch2 = '/^NTP\sWARNING:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2}/';
my $ntp_critmatch2 = '/^NTP\sCRITICAL:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2}/';

foreach my $plugin (@PLUGINS1) {
	SKIP: {
		skip "No NTP server defined", 1 unless $ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000"
			);
		cmp_ok( $res->return_code, '==', 0, "$plugin: Got good NTP result");
		like( $res->output, $ntp_okmatch1, "Output OK" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000: -c 2000"
			);
		cmp_ok( $res->return_code, '==', 1, "$plugin: Got warning NTP result");
		like( $res->output, $ntp_warnmatch1, "Output WARNING" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000:"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: Got critical NTP result");
		like( $res->output, $ntp_critmatch1, "Output CRITICAL" );
	}

	SKIP: {
		skip "No bad NTP server defined", 1 unless $no_ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $no_ntp_service"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: Got bad NTP result");
	}

	$res = NPTest->testCmd(
		"./$plugin -H $host_nonresponsive"
		);
	cmp_ok( $res->return_code, '==', 2, "$plugin: Got critical if server not responding");

	$res = NPTest->testCmd(
		"./$plugin -H $hostname_invalid"
		);
	cmp_ok( $res->return_code, '==', 3, "$plugin: Got critical if server hostname invalid");

}

foreach my $plugin (@PLUGINS2) {
	SKIP: {
		skip "No NTP server defined", 1 unless $ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W 20 -C 21 -j 100000 -k 200000"
			);
		cmp_ok( $res->return_code, '==', 0, "$plugin: Got good NTP result");
		like( $res->output, $ntp_okmatch2, "Output OK" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W ~:-1 -C 21 -j 100000 -k 200000"
			);
		cmp_ok( $res->return_code, '==', 1, "$plugin: Got warning NTP result");
		like( $res->output, $ntp_warnmatch2, "Output WARNING" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W 20 -C 21 -j 100000 -k ~:-1"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: Got critical NTP result");
		like( $res->output, $ntp_critmatch2, "Output CRITICAL" );
	}
}
