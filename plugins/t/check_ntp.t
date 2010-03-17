#! /usr/bin/perl -w -I ..
#
# Testing NTP
#
#

use strict;
use Test::More;
use NPTest;

my @PLUGINS1 = ('check_ntp', 'check_ntp_peer', 'check_ntp_time');
my @PLUGINS2 = ('check_ntp_peer');

plan tests => (12 * scalar(@PLUGINS1)) + (6 * scalar(@PLUGINS2));

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
my $ntp_okmatch2 = '/^NTP\sOK:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2},\struechimers=[0-9]+/';
my $ntp_warnmatch2 = '/^NTP\sWARNING:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2},\struechimers=[0-9]+/';
my $ntp_critmatch2 = '/^NTP\sCRITICAL:\sOffset\s-?[0-9]+(\.[0-9]+)?(e-[0-9]{2})?\ssecs,\sjitter=[0-9]+\.[0-9]+,\sstratum=[0-9]{1,2},\struechimers=[0-9]+/';
my $ntp_noresponse = '/^(CRITICAL - Socket timeout after 3 seconds)|(NTP CRITICAL: No response from NTP server)$/';
my $ntp_nosuchhost = '/^check_ntp.*: Invalid hostname/address - ' . $hostname_invalid . '/';


foreach my $plugin (@PLUGINS1) {
	SKIP: {
		skip "No NTP server defined", 1 unless $ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000"
			);
		cmp_ok( $res->return_code, '==', 0, "$plugin: Good NTP result (simple check)" );
		like( $res->output, $ntp_okmatch1, "$plugin: Output match OK (simple check)" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000: -c 2000"
			);
		cmp_ok( $res->return_code, '==', 1, "$plugin: Warning NTP result (simple check)" );
		like( $res->output, $ntp_warnmatch1, "$plugin: Output match WARNING (simple check)" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000:"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: Critical NTP result (simple check)" );
		like( $res->output, $ntp_critmatch1, "$plugin: Output match CRITICAL (simple check)" );
	}

	SKIP: {
		skip "No bad NTP server defined", 1 unless $no_ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $no_ntp_service -t 3"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: No NTP service" );
		like( $res->output, $ntp_noresponse, "$plugin: Output match no NTP service" );
	}

	$res = NPTest->testCmd(
		"./$plugin -H $host_nonresponsive -t 3"
		);
	cmp_ok( $res->return_code, '==', 2, "$plugin: Server not responding" );
	like( $res->output, $ntp_noresponse, "$plugin: Output match non-responsive" );

	$res = NPTest->testCmd(
		"./$plugin -H $hostname_invalid"
		);
	cmp_ok( $res->return_code, '==', 3, "$plugin: Invalid hostname/address" );
	like( $res->output, $ntp_nosuchhost, "$plugin: Output match invalid hostname/address" );

}

foreach my $plugin (@PLUGINS2) {
	SKIP: {
		skip "No NTP server defined", 1 unless $ntp_service;
		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W 20 -C 21 -j 100000 -k 200000 -m 1: -n 0:"
			);
		cmp_ok( $res->return_code, '==', 0, "$plugin: Good NTP result with jitter, stratum, and truechimers check" );
		like( $res->output, $ntp_okmatch2, "$plugin: Output match OK with jitter, stratum, and truechimers" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W \\~:-1 -C 21 -j 100000 -k 200000 -m 1: -n 0:"
			);
		cmp_ok( $res->return_code, '==', 1, "$plugin: Warning NTP result with jitter, stratum, and truechimers check" );
		like( $res->output, $ntp_warnmatch2, "$plugin: Output match WARNING with jitter, stratum, and truechimers" );

		$res = NPTest->testCmd(
			"./$plugin -H $ntp_service -w 1000 -c 2000 -W 20 -C 21 -j 100000 -k \\~:-1 -m 1: -n 0:"
			);
		cmp_ok( $res->return_code, '==', 2, "$plugin: Critical NTP result with jitter, stratum, and truechimers check" );
		like( $res->output, $ntp_critmatch2, "$plugin: Output match CRITICAL with jitter, stratum, and truechimers" );
	}
}
