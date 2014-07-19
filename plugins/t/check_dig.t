#! /usr/bin/perl -w -I ..
#
# Domain Name Server (DNS) Tests via check_dig
#
#

use strict;
use Test::More;


use vars qw($tests $has_ipv6);
BEGIN {
    plan skip_all => "check_dig not compiled" unless (-x "check_dig");
    use NPTest;
    $has_ipv6 = NPTest::has_ipv6();
    $tests = $has_ipv6 ? 16 : 14;
    plan tests => $tests;
}

my $successOutput = '/DNS OK - [\.0-9]+ seconds? response time/';

my $hostname_valid = getTestParameter(
			"MP_HOSTNAME_VALID",
			"A valid (known to DNS) hostname",
			"orwell.monitoring-plugins.org"
			);

my $hostname_valid_ip = getTestParameter(
			"MP_HOSTNAME_VALID_IP",
			"The IP address of the valid hostname $hostname_valid",
			"130.133.8.40",
			);

my $hostname_valid_reverse = getTestParameter(
			"MP_HOSTNAME_VALID_REVERSE",
			"The hostname of $hostname_valid_ip",
			"orwell.monitoring-plugins.org.",
			);

my $hostname_invalid = getTestParameter(
			"MP_HOSTNAME_INVALID",
			"An invalid (not known to DNS) hostname",
			"nosuchhost.monitoring-plugins.org",
			);

my $dns_server       = getTestParameter(
			"MP_DNS_SERVER",
			"A non default (remote) DNS server",
			);

my $res;

SKIP: {
        skip "check_dig.t: not enough parameters given",
	12 unless ($hostname_valid && $hostname_valid_ip && $hostname_valid_reverse && $hostname_invalid && $dns_server);

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid -t 5");
	cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid");
	like  ( $res->output, $successOutput, "Output OK" );

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid -t 5 -w 0.000001 -c 0.00001");
	cmp_ok( $res->return_code, '==', 2, "Critical threshold passed");

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid -t 5 -w 0.000001 -c 5");
	cmp_ok( $res->return_code, '==', 1, "Warning threshold passed");

	$res = NPTest->testCmd("./check_dig -H $dns_server -t 1");
	cmp_ok( $res->return_code, '==', 3, "Invalid command line -l missing");

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_invalid -t 1");
	cmp_ok( $res->return_code, '==', 2, "Invalid $hostname_invalid");

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid  -t 5");
	cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid on $dns_server");
	like  ( $res->output, $successOutput, "Output OK" );

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid  -t 5 -4");
	cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid on $dns_server");
	like  ( $res->output, $successOutput, "Output OK for IPv4" );

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid -a $hostname_valid_ip -t 5");
	cmp_ok( $res->return_code, '==', 0, "Got expected address");

	$res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid -a 10.10.10.10 -t 5");
	cmp_ok( $res->return_code, '==', 1, "Got wrong address");

	my $ip_reverse = $hostname_valid_ip;
	$ip_reverse =~ s/(\d+)\.(\d+)\.(\d+)\.(\d+)/$4.$3.$2.$1.in-addr.arpa/;
	$res = NPTest->testCmd("./check_dig -H $dns_server -l $ip_reverse -a $hostname_valid_reverse -T PTR -t 5");
	cmp_ok( $res->return_code, '==', 0, "Got expected fqdn");
	like  ( $res->output, $successOutput, "Output OK");

    if($has_ipv6) {
	    $res = NPTest->testCmd("./check_dig -H $dns_server -l $hostname_valid  -t 5 -6");
	    cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid on $dns_server");
	    like  ( $res->output, $successOutput, "Output OK for IPv6" );
    }
}
