#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_http
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

my $successOutput = '/OK.*HTTP.*second/';

my $res;

my $host_tcp_http      = getTestParameter( "NP_HOST_TCP_HTTP", 
		"A host providing the HTTP Service (a web server)", 
		"localhost" );

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE", 
		"The hostname of system not responsive to network requests",
		"10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID", 
		"An invalid (not known to DNS) hostname",  
		"nosuchhost");

plan tests => 8;


$res = NPTest->testCmd(
	"./check_http $host_tcp_http -wt 300 -ct 600"
	);
cmp_ok( $res->return_code, '==', 0, "Webserver $host_tcp_http responded" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_http $host_nonresponsive -wt 1 -ct 2"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $host_nonresponsive not responding" );
cmp_ok( $res->output, 'eq', "CRITICAL - Socket timeout after 10 seconds", "Output OK");

$res = NPTest->testCmd(
	"./check_http $hostname_invalid -wt 1 -ct 2"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $hostname_invalid not valid" );
like( $res->output, "/Name or service not known.*/", "Output OK");

$res = NPTest->testCmd(
	"./check_http --ssl www.verisign.com"
	);
cmp_ok( $res->return_code, '==', 0, "Can read https for www.verisign.com" );

$res = NPTest->testCmd(
	"./check_http --ssl www.e-paycobalt.com"
	);
cmp_ok( $res->return_code, "==", 0, "Can read https for www.e-paycobalt.com (uses AES certificate)" );
