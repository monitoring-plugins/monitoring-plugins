#! /usr/bin/perl -w -I ..
#
# SNMP Test via check_ifoperstatus
#
#

use strict;
use Test::More;
use NPTest;

my $tests = 9;
plan tests => $tests;
my $res;

my $plugin = "check_ifstatus";
SKIP: {
	skip "$plugin is not created", $tests if ( ! -x $plugin );

	my $host_snmp = getTestParameter( "NP_HOST_SNMP", "A host providing an SNMP Service", "localhost");

	my $snmp_community = getTestParameter( "NP_SNMP_COMMUNITY",
	                                       "The SNMP Community string for SNMP Testing",
	                                       "public");

	my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE",
	                                           "The hostname of system not responsive to network requests", "10.0.0.1" );

	my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
	                                           "An invalid (not known to DNS) hostname",
	                                           "nosuchhost" );


	$res = NPTest->testCmd( "./$plugin" );
	is( $res->return_code, 3, "No arguments" );
	like( $res->output, '/usage/', "Output contains usage" );
	
	$res = NPTest->testCmd( "./$plugin -H fakehost -v 3 --seclevel rubbish --secname foobar" );
	is( $res->return_code, 3, "invalid seclevel" );
	like( $res->output, "/Must define a valid security level/", "Output contains 'Must define a valid security level'" );

	SKIP: {
		skip "no snmp host defined", 2 if ( ! $host_snmp );

		$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community ");
		like($res->output, '/^.*host.*interfaces up/', "String contains host.*interfaces up");

		$res = NPTest->testCmd( "./$plugin -H $host_snmp -C rubbish");
		cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL for community 'rubbish'" ); 

	}

	# These checks need a complete command line. An invalid community is used so
	# the tests can run on hosts w/o snmp host/community in NPTest.cache. Execution will fail anyway
	SKIP: {
		skip "no non responsive host defined", 1 if ( ! $host_nonresponsive );
		$res = NPTest->testCmd( "./$plugin -H $host_nonresponsive -C np_foobar");
		cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL with non responsive host" ); 
	}

	SKIP: {
		skip "no invalid host defined", 2 if ( ! $hostname_invalid );
		$res = NPTest->testCmd( "./$plugin -H $hostname_invalid -C np_foobar");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with invalid host" ); 
		like($res->output, "/Unable to resolve.*$hostname_invalid/", "String matches unable to resolve.*$hostname_invalid");
	}

}
