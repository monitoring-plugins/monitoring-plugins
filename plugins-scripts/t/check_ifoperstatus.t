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

	my $host_snmp = getTestParameter( "NP_HOST_SNMP", "A host providing an SNMP Service", "localhost");

	my $snmp_community = getTestParameter( "NP_SNMP_COMMUNITY",
	                                       "The SNMP Community string for SNMP Testing",
	                                       "public");

	my ($snmp_interface, $snmp_ifxtable);
	if ($host_snmp) {
		$snmp_interface   = getTestParameter( "NP_SNMP_INTERFACE", "Name of an active network interface on SNMP server", "lo" );

		$snmp_ifxtable   = getTestParameter( "NP_SNMP_IFXTABLE",   
		                                     "Is IFXTABLE activated in SNMP server (1: yes, 0: no)? snmpwalk -v1 -c $snmp_community $host_snmp ifxtable",
		                                     "1" );
	}

	my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE", 
	                                           "The hostname of system not responsive to network requests", "10.0.0.1" );

	my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
	                                           "An invalid (not known to DNS) hostname",
	                                           "nosuchhost" );



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

		SKIP: {
			skip "no snmp interface defined", 2 if ( ! $snmp_interface );
			$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community -d $snmp_interface");
			cmp_ok( $res->return_code, '==', 0, "Exit OK for ifdescr $snmp_interface" );
			like($res->output, '/^OK.*Interface.*is up/', "String contains OK Interface is up");
		}

		SKIP: {
			skip "ifxtable not available", 2 if ( ! $snmp_ifxtable );
			$res = NPTest->testCmd( "./$plugin -H $host_snmp -C $snmp_community -k 1 -n rubbish");
			cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN if interface name doesn't match" ); 
			like($res->output, '/doesn\'t match snmp value/', "String contains 'doesn't match snmp value'");
		}

	}

	# These checks need a complete command line. An invalid community is used so
	# the tests can run on hosts w/o snmp host/community in NPTest.cache. Execution will fail anyway
	SKIP: {
		skip "no non responsive host defined", 1 if ( ! $host_nonresponsive );
		$res = NPTest->testCmd( "./$plugin -H $host_nonresponsive -C np_foobar -k 1");
		cmp_ok( $res->return_code, '==', 1, "Exit WARNING with non responsive host" ); 
	}

	SKIP: {
		skip "no invalid host defined", 2 if ( ! $hostname_invalid );
		$res = NPTest->testCmd( "./$plugin -H $hostname_invalid -C np_foobar -k 1");
		cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with invalid host" ); 
		like($res->output, "/Unable to resolve.*$hostname_invalid/", "String matches unable to resolve.*$hostname_invalid");
	}

}
