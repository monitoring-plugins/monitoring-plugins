#! /usr/bin/perl -w -I ..
#
# check_ssh tests
#
#

use strict;
use warnings;
use Test::More;
use NPTest;
use JSON;

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

	my $outputFormat = '--output-format mp-test-json';

plan tests => 24;

my $output;
my $result;

SKIP: {
	skip "SSH_HOST must be defined", 6 unless $ssh_host;


	my $result = NPTest->testCmd(
		"./check_ssh -H $ssh_host" ." ". $outputFormat
		);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "OK", "State was correct");


	$result = NPTest->testCmd(
		"./check_ssh -H $host_nonresponsive -t 2" ." ". $outputFormat
		);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "CRITICAL", "State was correct");



	$result = NPTest->testCmd(
		"./check_ssh -H $hostname_invalid -t 2" ." ". $outputFormat
		);
	cmp_ok($result->return_code, '==', 3, "Exit with return code 3 (UNKNOWN)");
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

	my $found_version = 0;

	open(NC, "echo 'SSH-2.0-nagiosplug.ssh.0.1' | nc ${nc_flags}|");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "OK", "State was correct");

	# looking for the version
	for my $subcheck (@{$output->{'checks'}}) {
		if ($subcheck->{'output'} =~ /.*nagiosplug.ssh.0.1 \(protocol version: 2.0\).*/ ){
			$found_version = 1;
		}
 	}
	cmp_ok($found_version, '==', 1, "Output OK");
	close NC;

	open(NC, "echo 'SSH-2.0-3.2.9.1' | nc ${nc_flags}|");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "OK", "State was correct");

	$found_version = 0;
	for my $subcheck (@{$output->{'checks'}}) {
		if ($subcheck->{'output'} =~ /3.2.9.1 \(protocol version: 2.0\)/ ){
			$found_version = 1;
		}
 	}
	cmp_ok($found_version, '==', 1, "Output OK");
	close NC;

	open(NC, "echo 'SSH-2.0-nagiosplug.ssh.0.1 this is a comment' | nc ${nc_flags} |");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003 -r nagiosplug.ssh.0.1" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "OK", "State was correct");

	# looking for the version
	$found_version = 0;
	for my $subcheck (@{$output->{'checks'}}) {
		if ($subcheck->{'output'} =~ /nagiosplug.ssh.0.1 \(protocol version: 2.0\)/ ){
			$found_version = 1;
		}
 	}
	cmp_ok($found_version, '==', 1, "Output OK");
	close NC;

	open(NC, "echo 'SSH-' | nc ${nc_flags}|");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "CRITICAL", "Got invalid SSH protocol version control string");
	close NC;

	open(NC, "echo '' | nc ${nc_flags}|");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "CRITICAL", "No version control string received");
	close NC;

	open(NC, "echo 'Not a version control string' | nc ${nc_flags}|");
	sleep 0.1;
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003"  ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "CRITICAL", "No version control string received");
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
	$result = NPTest->testCmd( "./check_ssh -H localhost -p 5003" ." ". $outputFormat);
	cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
	$output = decode_json($result->output);
	is($output->{'state'}, "OK", "State was correct");

	# looking for the version
	$found_version = 0;
	for my $subcheck (@{$output->{'checks'}}) {
		if ($subcheck->{'output'} =~ /nagiosplug.ssh.0.2 \(protocol version: 2.0\)/ ){
			$found_version = 1;
		}
 	}
	cmp_ok($found_version, '==', 1, "Output OK");
	close NC;
}
