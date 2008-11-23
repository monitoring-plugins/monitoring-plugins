#! /usr/bin/perl -w -I ..
#
# Domain Name Server (DNS) Tests via check_dns
#
#

use strict;
use Test::More;
use NPTest;

plan skip_all => "check_dns not compiled" unless (-x "check_dns");

plan tests => 13;

my $successOutput = '/DNS OK: [\.0-9]+ seconds? response time/';

my $hostname_valid = getTestParameter( 
			"NP_HOSTNAME_VALID",
			"A valid (known to DNS) hostname",
			"nagios.com"
			);

my $hostname_valid_ip = getTestParameter(
			"NP_HOSTNAME_VALID_IP",
			"The IP address of the valid hostname $hostname_valid",
			"66.118.156.50",
			);

my $hostname_valid_reverse = getTestParameter(
			"NP_HOSTNAME_VALID_REVERSE",
			"The hostname of $hostname_valid_ip",
			"66-118-156-50.static.sagonet.net.",
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

my $res;

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5");
cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid");
like  ( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5 -w 0 -c 0");
cmp_ok( $res->return_code, '==', 2, "Critical threshold passed");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -t 5 -w 0 -c 5");
cmp_ok( $res->return_code, '==', 1, "Warning threshold passed");

$res = NPTest->testCmd("./check_dns -H $hostname_invalid -t 1");
cmp_ok( $res->return_code, '==', 2, "Invalid $hostname_invalid");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -s $dns_server -t 5");
cmp_ok( $res->return_code, '==', 0, "Found $hostname_valid on $dns_server");
like  ( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd("./check_dns -H $hostname_invalid -s $dns_server -t 1");
cmp_ok( $res->return_code, '==', 2, "Invalid $hostname_invalid on $dns_server");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a $hostname_valid_ip -t 5");
cmp_ok( $res->return_code, '==', 0, "Got expected address");

$res = NPTest->testCmd("./check_dns -H $hostname_valid -a 10.10.10.10 -t 5");
cmp_ok( $res->return_code, '==', 2, "Got wrong address");
like  ( $res->output, "/^DNS CRITICAL.*expected '10.10.10.10' but got '$hostname_valid_ip'".'$/', "Output OK");

$res = NPTest->testCmd("./check_dns -H $hostname_valid_ip -a $hostname_valid_reverse -t 5");
cmp_ok( $res->return_code, '==', 0, "Got expected fqdn");
like  ( $res->output, $successOutput, "Output OK");

