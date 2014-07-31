#! /usr/bin/perl -w -I ..
#
# test cases for check_disk_smb
#

use strict;
use Test::More;
use NPTest;

my $tests = 14;
plan tests => $tests;
my $res;

my $plugin = "check_disk_smb";
SKIP: {
	skip "$plugin is not created", $tests if ( ! -x $plugin );
	my $auth = "";

	my $host = getTestParameter("NP_HOST_SMB", "A host providing an SMB Service",
	                            "localhost");

	my $smb_share = getTestParameter("NP_SMB_SHARE",
	                                 "An SMB share name the host provides",
	                                 "public");

	my $smb_share_spc = getTestParameter("NP_SMB_SHARE_SPC",
	                                     "An SMB share name containing one or more spaces the host provides",
	                                     "pub lic");

	my $smb_share_deny = getTestParameter("NP_SMB_SHARE_DENY",
	                                      "An access denying SMB share name the host provides",
	                                      "private");

	my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE",
	                                           "The hostname of system not responsive to network requests", "10.0.0.1" );

	my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
	                                           "An invalid (not known to DNS) hostname",
	                                           "nosuchhost" );
	my $user = getTestParameter( "NP_SMB_VALID_USER", "A valid smb user", "" );
	my $pass = getTestParameter( "NP_SMB_VALID_USER_PASS", "A valid password for valid smb user", "" );
	$auth .= "-u $user " if ($user);
	$auth .= "-p $pass " if ($pass);



	$res = NPTest->testCmd( "./$plugin" );
	is( $res->return_code, 3, "No arguments" );

	$res = NPTest->testCmd( "./$plugin -H fakehostname" );
	is( $res->return_code, 3, "No share specified" );

	$res = NPTest->testCmd( "./$plugin -H fakehostname -s share -w 100G -c 101G" );
	is( $res->return_code, 3, "warn is less than critical" );

	SKIP: {
		skip "no smb host defined", 10 if ( ! $host );

		SKIP: {
			skip "no share name defined", 2 if ( ! $smb_share );
			$res = NPTest->testCmd( "./$plugin -H $host $auth -s $smb_share -w 2k -c 1k" );
			cmp_ok( $res->return_code, '==', 0, "Exit OK if $smb_share has > 1k free space");
			like($res->output, '/free/i', "String contains the word 'free'");

			$res = NPTest->testCmd( "./$plugin -H $host $auth -s $smb_share -w 10001G -c 10000G" );
			cmp_ok( $res->return_code, '==', 2, "Exit CRIT if $smb_share has < 10000G free space");
			like($res->output, '/free/i', "String contains the word 'free'");

			$res = NPTest->testCmd( "./$plugin -H $host $auth -s $smb_share -w 10000G -c 1k" );
			cmp_ok( $res->return_code, '==', 1, "Exit WARN if $smb_share has > 10000G and <1k free space");
			like($res->output, '/free/i', "String contains the word 'free'");
		}

		SKIP: {
			skip "no share name containing spaces defined", 2 if ( ! $smb_share_spc );
			$res = NPTest->testCmd( "./$plugin -H $host $auth -s '$smb_share_spc' -w 2k -c 1k" );
			cmp_ok( $res->return_code, '==', 0, "Exit OK if '$smb_share_spc' has > 1k free space");
			like($res->output, '/free/i', "String contains the word 'free'");

		}
		SKIP: {
			skip "no share name without permissions ", 2 if ( ! $smb_share_deny );
			$res = NPTest->testCmd( "./$plugin -H $host $auth -s $smb_share_deny -w 2k -c 1k" );
			cmp_ok( $res->return_code, '==', 2, "Exit CRIT if $smb_share_deny has > 1k free space");
			unlike($res->output, '/free/i', "String does not contain the word 'free'");

		}
	}

	SKIP: {
		skip "no non responsive host defined", 1 if ( ! $host_nonresponsive );
		$res = NPTest->testCmd( "./$plugin -H $host_nonresponsive -s np_foobar ");
		cmp_ok( $res->return_code, '>=', 2, "Exit CRITICAL/UNKNOWN with non responsive host" );
	}

}
