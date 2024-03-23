#! /usr/bin/perl -w -I ..
#
# check_ssh tests
#
#

use strict;
use Test::More;
use NPTest;

my $res;

# Required parameters
my $ssh_host           = getTestParameter("NP_SSH_HOST",
                                          "A host providing SSH service",
                                          "localhost");

my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE",
                                          "The hostname of system not responsive to network requests",
                                          "10.0.0.1" );

my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID",
                                          "An invalid (not known to DNS) hostname",
                                          "nosuchhost" );


plan tests => 14 + 6;

SKIP: {
	skip "SSH_HOST must be defined", 6 unless $ssh_host;
	my $result = NPTest->testCmd(
		"./check_ssh -H $ssh_host"
		);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	like($result->output, '/^SSH OK - /', "Status text if command returned none (OK)");


	$result = NPTest->testCmd(
		"./check_ssh -H $host_nonresponsive -t 2"
		);
	cmp_ok($result->return_code, '==', 2, "Exit with return code 0 (OK)");
	like($result->output, '/^CRITICAL - Socket timeout after 2 seconds/', "Status text if command returned none (OK)");



	$result = NPTest->testCmd(
		"./check_ssh -H $hostname_invalid -t 2"
		);
	cmp_ok($result->return_code, '==', 3, "Exit with return code 0 (OK)");
	like($result->output, '/^check_ssh: Invalid hostname/', "Status text if command returned none (OK)");


}
SKIP: {

	skip "No netcat available", 14 unless (system("which nc > /dev/null") == 0);

	# netcat on linux (on debian) will just keep the socket open if not advised otherwise
	# therefore we add -q to close it after two seconds after receiving the EOF from input
	my $nc_flags = "-l 5003 -N";
	#A valid protocol version control string has the form
	#       SSH-protoversion-softwareversion SP comments CR LF
	#
	# where `comments` is optional, protoversion is the SSH protocol version and
	# softwareversion is an arbitrary string representing the server software version
	open(NC, "echo 'SSH-2.0-nagiosplug.ssh.0.1' | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, '==', 0, "Got SSH protocol version control string");
	like( $res->output, '/^SSH OK - nagiosplug.ssh.0.1 \(protocol 2.0\)/', "Output OK");
	close NC;

	open(NC, "echo 'SSH-2.0-3.2.9.1' | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, "==", 0, "Got SSH protocol version control string with non-alpha softwareversion string");
	like( $res->output, '/^SSH OK - 3.2.9.1 \(protocol 2.0\)/', "Output OK for non-alpha softwareversion string");
	close NC;

	open(NC, "echo 'SSH-2.0-nagiosplug.ssh.0.1 this is a comment' | nc ${nc_flags} |");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003 -r nagiosplug.ssh.0.1" );
	cmp_ok( $res->return_code, '==', 0, "Got SSH protocol version control string, and parsed comment appropriately");
	like( $res->output, '/^SSH OK - nagiosplug.ssh.0.1 \(protocol 2.0\)/', "Output OK");
	close NC;

	open(NC, "echo 'SSH-' | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, '==', 2, "Got invalid SSH protocol version control string");
	like( $res->output, '/^SSH CRITICAL/', "Output OK");
	close NC;

	open(NC, "echo '' | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, '==', 2, "No version control string received");
	like( $res->output, '/^SSH CRITICAL - No version control string received/', "Output OK");
	close NC;

	open(NC, "echo 'Not a version control string' | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, '==', 2, "No version control string received");
	like( $res->output, '/^SSH CRITICAL - No version control string received/', "Output OK");
	close NC;


	#RFC 4253 permits servers to send any number of data lines prior to sending the protocol version control string
	open(NC, "{ echo 'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'; sleep 0.5;
		echo 'BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB'; sleep 0.5;
		echo 'CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC'; sleep 0.2;
		echo 'DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD'; sleep 0.3;
		printf 'EEEEEEEEEEEEEEEEEE'; sleep 0.2;
		printf 'EEEEEEEEEEEEEEEEEE\n'; sleep 0.2;
		echo 'Some\nPrepended\nData\nLines\n'; sleep 0.2;
		echo 'SSH-2.0-nagiosplug.ssh.0.2';} | nc ${nc_flags}|");
	sleep 0.1;
	$res = NPTest->testCmd( "./check_ssh -H localhost -p 5003" );
	cmp_ok( $res->return_code, '==', 0, "Got delayed SSH protocol version control string");
	like( $res->output, '/^SSH OK - nagiosplug.ssh.0.2 \(protocol 2.0\)/', "Output OK");
	close NC;
}
