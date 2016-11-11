#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_http
#
#

use strict;
use Test::More;
use POSIX qw/mktime strftime/;
use NPTest;

plan tests => 49;

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
            "A host providing an index page containing the string 'monitoring'",
            "test.monitoring-plugins.org" );

my $faketime = -x '/usr/bin/faketime' ? 1 : 0;


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
	"./check_http $host_nonresponsive -wt 1 -ct 2 -t 3"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $host_nonresponsive not responding" );
cmp_ok( $res->output, 'eq', "CRITICAL - Socket timeout after 3 seconds", "Output OK");

$res = NPTest->testCmd(
	"./check_http $hostname_invalid -wt 1 -ct 2"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $hostname_invalid not valid" );
# The first part of the message comes from the OS catalogue, so cannot check this.
# On Debian, it is Name or service not known, on Darwin, it is No address associated with nodename
# Is also possible to get a socket timeout if DNS is not responding fast enough
like( $res->output, "/Unable to open TCP socket|Socket timeout after/", "Output OK");

# host header checks
$res = NPTest->testCmd("./check_http -v -H $host_tcp_http");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./check_http -v -H $host_tcp_http -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./check_http -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./check_http -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );

SKIP: {
        skip "No internet access", 3 if $internet_access eq "no";

        $res = NPTest->testCmd("./check_http -v -H www.verisign.com -S");
        like( $res->output, '/^Host: www.verisign.com\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./check_http -v -H www.verisign.com:8080 -S -p 443");
        like( $res->output, '/^Host: www.verisign.com:8080\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./check_http -v -H www.verisign.com:443 -S -p 443");
        like( $res->output, '/^Host: www.verisign.com\s*$/ms', "Host Header OK" );
};

SKIP: {
        skip "No host serving monitoring in index file", 7 unless $host_tcp_http2;

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'monitoring'" );
        cmp_ok( $res->return_code, "==", 0, "Got a reference to 'monitoring'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 2, "Not got 'mONiTORing'");
        like ( $res->output, "/pattern not found/", "Error message says 'pattern not found'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -R 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 0, "But case insensitive doesn't mind 'mONiTORing'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'monitoring' --invert-regex" );
        cmp_ok( $res->return_code, "==", 2, "Invert results work when found");
        like ( $res->output, "/pattern found/", "Error message says 'pattern found'");

        $res = NPTest->testCmd( "./check_http -H $host_tcp_http2 -r 'mONiTORing' --invert-regex" );
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

        # run some certificate checks with faketime
        SKIP: {
                skip "No faketime binary found", 12 if !$faketime;
                $res = NPTest->testCmd("LC_TIME=C TZ=UTC ./check_http -C 1 www.verisign.com");
                like($res->output, qr/OK - Certificate 'www.verisign.com' will expire on/, "Catch cert output");
                is( $res->return_code, 0, "Catch cert output exit code" );
                my($mon,$day,$hour,$min,$sec,$year) = ($res->output =~ /(\w+)\s+(\d+)\s+(\d+):(\d+):(\d+)\s+(\d+)/);
                if(!defined $year) {
                    die("parsing date failed from: ".$res->output);
                }
                my $months = {'Jan' => 0, 'Feb' => 1, 'Mar' => 2, 'Apr' => 3, 'May' => 4, 'Jun' => 5, 'Jul' => 6, 'Aug' => 7, 'Sep' => 8, 'Oct' => 9, 'Nov' => 10, 'Dec' => 11};
                my $ts   = mktime($sec, $min, $hour, $day, $months->{$mon}, $year-1900);
                my $time = strftime("%Y-%m-%d %H:%M:%S", localtime($ts));
                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./check_http -C 1 www.verisign.com");
                like($res->output, qr/CRITICAL - Certificate 'www.verisign.com' just expired/, "Output on expire date");
                is( $res->return_code, 2, "Output on expire date" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-1))."' ./check_http -C 1 www.verisign.com");
                like($res->output, qr/CRITICAL - Certificate 'www.verisign.com' expires in 0 minutes/, "cert expires in 1 second output");
                is( $res->return_code, 2, "cert expires in 1 second exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-120))."' ./check_http -C 1 www.verisign.com");
                like($res->output, qr/CRITICAL - Certificate 'www.verisign.com' expires in 2 minutes/, "cert expires in 2 minutes output");
                is( $res->return_code, 2, "cert expires in 2 minutes exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-7200))."' ./check_http -C 1 www.verisign.com");
                like($res->output, qr/CRITICAL - Certificate 'www.verisign.com' expires in 2 hours/, "cert expires in 2 hours output");
                is( $res->return_code, 2, "cert expires in 2 hours exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./check_http -C 1 www.verisign.com");
                like($res->output, qr/CRITICAL - Certificate 'www.verisign.com' expired on/, "Certificate expired output");
                is( $res->return_code, 2, "Certificate expired exit code" );
        };

        $res = NPTest->testCmd( "./check_http --ssl www.verisign.com -E" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
        like  ( $res->output, '/time_ssl=[\d\.]+/', 'Extended Performance Data SSL Output OK' );

        $res = NPTest->testCmd(
                "./check_http --ssl -H www.e-paycobalt.com"
                );
        cmp_ok( $res->return_code, "==", 0, "Can read https for www.e-paycobalt.com (uses AES certificate)" );


        $res = NPTest->testCmd( "./check_http -H www.mozilla.com -u /firefox -f follow" );
        is( $res->return_code, 0, "Redirection based on location is okay");

        $res = NPTest->testCmd( "./check_http -H www.mozilla.com --extended-perfdata" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
}
