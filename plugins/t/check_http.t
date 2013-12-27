#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_http
#
#

use strict;
use Test::More;
use NPTest;

plan tests => 30;

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

my $internet_access = getTestParameter( "NP_INTERNET_ACCESS",
                "Is this system directly connected to the internet?",
                "yes");

my $host_tcp_http2  = getTestParameter( "NP_HOST_TCP_HTTP2",
            "A host providing an index page containing the string 'nagios'",
            "nagios.org" );


$res = NPTest->testCmd(
	"./check_http $host_tcp_http -wt 300 -ct 600"
	);
cmp_ok( $res->return_code, '==', 0, "Webserver $host_tcp_http responded" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"./check_http $host_tcp_http -wt 300 -ct 600 -v -v -v -k 'bob:there' -k 'carl:frown'"
	);
like( $res->output, '/bob:there\r\ncarl:frown\r\n/', "Got headers with multiple -k options" );

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

SKIP: {
        skip "No host serving nagios in index file", 7 unless $host_tcp_http2;

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'nagios'" );
        cmp_ok( $res->return_code, "==", 0, "Got a reference to 'nagios'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'nAGiOs'" );
        cmp_ok( $res->return_code, "==", 2, "Not got 'nAGiOs'");
        like ( $res->output, "/pattern not found/", "Error message says 'pattern not found'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -R 'nAGiOs'" );
        cmp_ok( $res->return_code, "==", 0, "But case insensitive doesn't mind 'nAGiOs'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'nagios' --invert-regex" );
        cmp_ok( $res->return_code, "==", 2, "Invert results work when found");
        like ( $res->output, "/pattern found/", "Error message says 'pattern found'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'nAGiOs' --invert-regex" );
        cmp_ok( $res->return_code, "==", 0, "And also when not found");
}
SKIP: {
        skip "No internet access", 16 if $internet_access eq "no";

        $res = NPTest->testCmd(
                "./check_http --ssl www.verisign.com"
                );
        cmp_ok( $res->return_code, '==', 0, "Can read https for www.verisign.com" );

        $res = NPTest->testCmd( "./check_http -C 1 --ssl www.verisign.com" );
        cmp_ok( $res->return_code, '==', 0, "Checking certificate for www.verisign.com");
        like  ( $res->output, "/Certificate 'www.verisign.com' will expire on/", "Output OK" );
        my $saved_cert_output = $res->output;

        $res = NPTest->testCmd( "./check_http -C 8000,1 --ssl www.verisign.com" );
        cmp_ok( $res->return_code, '==', 1, "Checking certificate for www.verisign.com");
        like  ( $res->output, qr/WARNING - Certificate 'www.verisign.com' expires in \d+ day/, "Output Warning" );

        $res = NPTest->testCmd( "./check_http www.verisign.com -C 1" );
        is( $res->return_code, 0, "Old syntax for cert checking okay" );
        is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./check_http -H www.verisign.com -C 1" );
        is( $res->return_code, 0, "Updated syntax for cert checking okay" );
        is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./check_http -C 1 www.verisign.com" );
        cmp_ok( $res->output, 'eq', $saved_cert_output, "--ssl option automatically added");

        $res = NPTest->testCmd( "./check_http www.verisign.com -C 1" );
        cmp_ok( $res->output, 'eq', $saved_cert_output, "Old syntax for cert checking still works");

        $res = NPTest->testCmd( "./check_http --ssl www.verisign.com -E" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
        like  ( $res->output, '/time_ssl=[\d\.]+/', 'Extended Performance Data SSL Output OK' );

        $res = NPTest->testCmd(
                "./check_http --ssl www.e-paycobalt.com"
                );
        cmp_ok( $res->return_code, "==", 0, "Can read https for www.e-paycobalt.com (uses AES certificate)" );


        $res = NPTest->testCmd( "./check_http -H www.mozilla.com -u /firefox -f follow" );
        is( $res->return_code, 0, "Redirection based on location is okay");

        $res = NPTest->testCmd( "./check_http -H www.mozilla.com --extended-perfdata" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
}
