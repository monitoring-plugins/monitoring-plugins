#! /usr/bin/perl -w -I ..
#
# SNMP Test via check_ifoperstatus
#
#

use strict;
use Test::More;
use NPTest;

my $tests = 15;
plan tests => $tests;
my $res;

my $plugin = "check_ifoperstatus";
SKIP: {
	skip "$plugin is not created", $tests if ( ! -x $plugin );

	my $host_snmp = getTestParameter( "host_snmp",          "NP_HOST_SNMP",      "localhost",
	                                   "A host providing an SNMP Service");

	my $snmp_community = getTestParameter( "snmp_community",     "NP_SNMP_COMMUNITY", "public",
                                           "The SNMP Community string for SNMP Testing (assumes snmp v1)" );

	my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
	                                           "The hostname of system not responsive to network requests" );

	my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
	                                           "An invalid (not known to DNS) hostname" );

	$res = NPTest->testCmd( "./$plugin" );
	is( $res->return_code, 3, "No arguments" );
	like( $res->output, '/usage/', "Output contains usage" );
	
	$res = NPTest->testCmd( "./$plugin -H fakehostname" );
	is( $res->return_code, 3, "No key/descr specified" );
	like( $res->output, '/Either a valid snmp key/', "Output contains 'Either a valid snmp key'" );

	$res = NPTest->testCmd( "./$plugin -H fakehost -k 1 -v 3 --seclevel rubbish --secname foobar" );
	is( $res->return_code, 3, "invalid seclevel" );
	like( $res->output, "/Must define a valid security level/", "Output contains 'Must define a valid security level'" );

	SKIP: {
		skip "no snmp host defined", 6 if ( ! $host_snmp );

		$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community -k 1");
		cmp_ok( $res->return_code, '==', 0, "Exit OK for ifindex 1" ); 
		like($res->output, '/^OK.*Interface.*is up/', "String contains OK Interface is up");

		$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community -d lo");
		cmp_ok( $res->return_code, '==', 0, "Exit OK for ifdescr lo" ); 
		like($res->output, '/^OK.*Interface.*is up/', "String contains OK Interface is up");

		$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community -k 1 -n rubbish");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN if interface name doesn't match" ); 
		like($res->output, '/doesn\'t match snmp value/', "String contains 'doesn't match snmp value'");

	}

	SKIP: {
		skip "no non responsive host defined", 1 if ( ! $host_nonresponsive );
		$res = NPTest->testCmd( "./$plugin -H $host_nonresponsive -C $snmp_community -k 1");
		cmp_ok( $res->return_code, '==', 1, "Exit WARNING with non responsive host" ); 
	}

	SKIP: {
		skip "no invalid host defined", 2 if ( ! $hostname_invalid );
		$res = NPTest->testCmd( "./$plugin -H $hostname_invalid -C $snmp_community -k 1");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with invalid host" ); 
		like($res->output, "/Unable to resolve.*$hostname_invalid/", "String matches unable to resolve.*$hostname_invalid");
	}

}
