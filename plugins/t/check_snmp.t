#! /usr/bin/perl -w -I ..
#
# Simple Network Management Protocol (SNMP) Test via check_snmp
#
#

use strict;
use Test::More;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 14; plan tests => $tests}

my $res;

SKIP: {
	skip "check_snmp is not compiled", $tests if ( ! -x "./check_snmp" );

	my $host_snmp = getTestParameter( "host_snmp",          "NP_HOST_SNMP",      "localhost",
	                                   "A host providing an SNMP Service");

	my $snmp_community = getTestParameter( "snmp_community",     "NP_SNMP_COMMUNITY", "public",
                                           "The SNMP Community string for SNMP Testing" );

	my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
	                                           "The hostname of system not responsive to network requests" );

	my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
	                                           "An invalid (not known to DNS) hostname" );

	SKIP: {
		skip "no snmp host defined", 10 if ( ! $host_snmp );

		$res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:");
		cmp_ok( $res->return_code, '==', 0, "Exit OK when querying uptime" ); 
		like($res->output, '/^SNMP OK - \d+/', "String contains SNMP OK");

		$res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysDescr.0");
		cmp_ok( $res->return_code, '==', 0, "Exit OK when querying sysDescr" ); 
		unlike($res->perf_output, '/sysDescr/', "Perfdata doesn't contain string values");

		$res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 1:1 -c 1:1");
		cmp_ok( $res->return_code, '==', 0, "Exit OK when querying hrSWRunIndex.1" ); 
		like($res->output, '/^SNMP OK - 1\s.*$/', "String fits SNMP OK and output format");

		$res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 0   -c 1:");
		cmp_ok( $res->return_code, '==', 1, "Exit WARNING when querying hrSWRunIndex.1 and warn-th doesn't apply " ); 
		like($res->output, '/^SNMP WARNING - \*1\*\s.*$/', "String matches SNMP WARNING and output format");

		$res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w  :0 -c 0");
		cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL when querying hrSWRunIndex.1 and crit-th doesn't apply" ); 
		like($res->output, '/^SNMP CRITICAL - \*1\*\s.*$/', "String matches SNMP CRITICAL and ouput format");
	}

	SKIP: {
		skip "no non responsive host defined", 2 if ( ! $host_nonresponsive );
		$res = NPTest->testCmd( "./check_snmp -H $host_nonresponsive -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with non responsive host" ); 
		like($res->output, '/SNMP problem - /', "String matches SNMP Problem");
	}

	SKIP: {
		skip "no non invalid host defined", 2 if ( ! $hostname_invalid );
		$res = NPTest->testCmd( "./check_snmp -H $hostname_invalid   -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with non responsive host" ); 
		like($res->output, '/SNMP problem - /', "String matches SNMP Problem");
	}

}
