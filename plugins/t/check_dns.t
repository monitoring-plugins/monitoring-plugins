#! /usr/bin/perl -w -I ..
#
# Domain Name Server (DNS) Tests via check_dns
#
#

use strict;
use Test::More;
use NPTest;

plan skip_all => "check_dns not compiled" unless (-x "check_dns");

plan tests => 19;

my $successOutput = '/DNS OK: [\.0-9]+ seconds? response time/';

my $hostname_valid = getTestParameter( 
			"NP_HOSTNAME_VALID",
			"A valid (known to DNS) hostname",
			"monitoring-plugins.org",
			);

my $hostname_valid_ip = getTestParameter(
			"NP_HOSTNAME_VALID_IP",
			"The IP address of the valid hostname $hostname_valid",
			"130.133.8.40",
			);

my $hostname_valid_cidr = getTestParameter(
			"NP_HOSTNAME_VALID_CIDR",
			"An valid CIDR range containing $hostname_valid_ip",
			"130.133.8.41/30",
			);

my $hostname_invalid_cidr = getTestParameter(
			"NP_HOSTNAME_INVALID_CIDR",
			"An (valid) CIDR range NOT containing $hostname_valid_ip",
			"130.133.8.39/30",
			);

my $hostname_valid_reverse = getTestParameter(
			"NP_HOSTNAME_VALID_REVERSE",
			"The hostname of $hostname_valid_ip",
			"orwell.monitoring-plugins.org.",
			);

my $hostname_invalid = getTestParameter( 
			"NP_HOSTNAME_INVALID", 
			"An invalid (not known to DNS) hostname",
			"nosuchhost.altinity.com",
			);

my $dns_server       = getTestParameter(
			"NP_DNS_SERVER",
			"A non default (remote) DNS server",
			);

my $host_nonresponsive = getTestParameter(
			"NP_HOST_NONRESPONSIVE",
			"The hostname of system not responsive to network requests",
			"10.0.0.1",
			);

my $res;

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5");
cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid");
like  ( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5 -w 0 -c 0");
cmp_ok( $res->return_code, '==', 2, "Critical threshold passed");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5 -w 0 -c 5");
cmp_ok( $res->return_code, '==', 1, "Warning threshold passed");
like( $res->output, '/\|time=[\d\.]+s;0.0*;5\.0*;0\.0*/', "Output performance data OK" );

$res = NPTest->testCmd("./check_dns -H $hostname_invalid -t 1");
cmp_ok( $res->return_code, '==', 2, "Invalid $hostname_invalid");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -s $dns_server -t 5");
cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid on $dns_server");
like  ( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd("./check_dns -H $hostname_invalid -s $dns_server -t 1");
cmp_ok( $res->return_code, '==', 2, "Invalid $hostname_invalid on $dns_server");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a $hostname_valid_ip -s $host_nonresponsive -t 2");
cmp_ok( $res->return_code, '==', 2, "Got no answer from unresponsive server");
like  ( $res->output, "/CRITICAL - /", "Output OK");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a $hostname_valid_ip -t 5");
cmp_ok( $res->return_code, '==', 0, "Got expected address");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a 10.10.10.10 -t 5");
cmp_ok( $res->return_code, '==', 2, "Got wrong address");
like  ( $res->output, "/^DNS CRITICAL.*expected '10.10.10.10' but got '$hostname_valid_ip'".'$/', "Output OK");

$res = NPTest->testCmd("./check_dns -H $hostname_valid_ip -a $hostname_valid_reverse -t 5");
cmp_ok( $res->return_code, '==', 0, "Got expected fqdn");
like  ( $res->output, $successOutput, "Output OK");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a $hostname_valid_cidr -t 5");
cmp_ok( $res->return_code, '==', 0, "Got expected address");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a $hostname_invalid_cidr -t 5");
cmp_ok( $res->return_code, '==', 2, "Got wrong address");
like  ( $res->output, "/^DNS CRITICAL.*expected '$hostname_invalid_cidr' but got '$hostname_valid_ip'".'$/', "Output OK");
