#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_http
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

plan tests => 12;

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
# The first part of the message comes from the OS catalogue, so cannot check this.
# On Debian, it is Name or service not known, on Darwin, it is No address associated with nodename
# Is also possible to get a socket timeout if DNS is not responding fast enough
like( $res->output, "/Unable to open TCP socket|Socket timeout after/", "Output OK");

$res = NPTest->testCmd(
	"./check_http --ssl www.verisign.com"
	);
cmp_ok( $res->return_code, '==', 0, "Can read https for www.verisign.com" );

$res = NPTest->testCmd( "./check_http -C 1 --ssl www.verisign.com" );
cmp_ok( $res->return_code, '==', 0, "Checking certificate for www.verisign.com");
like  ( $res->output, '/Certificate will expire on/', "Output OK" );
my $saved_cert_output = $res->output;

$res = NPTest->testCmd( "./check_http -C 1 www.verisign.com" );
cmp_ok( $res->output, 'eq', $saved_cert_output, "--ssl option automatically added");

$res = NPTest->testCmd( "./check_http www.verisign.com -C 1" );
cmp_ok( $res->output, 'eq', $saved_cert_output, "Old syntax for cert checking still works");

$res = NPTest->testCmd(
	"./check_http --ssl www.e-paycobalt.com"
	);
cmp_ok( $res->return_code, "==", 0, "Can read https for www.e-paycobalt.com (uses AES certificate)" );

	
