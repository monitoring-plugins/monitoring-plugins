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
my $plugin = 'check_http';
$plugin    = 'check_curl' if $0 =~ m/check_curl/mx;

my $host_tcp_http      = getTestParameter("NP_HOST_TCP_HTTP", "A host providing the HTTP Service (a web server)", "localhost");
my $host_tls_http      = getTestParameter("NP_HOST_TLS_HTTP", "A host providing the HTTPS Service (a tls web server)", "localhost");
my $host_tls_cert      = getTestParameter("NP_HOST_TLS_CERT", "the common name of the certificate.", "localhost");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");
my $internet_access    = getTestParameter("NP_INTERNET_ACCESS", "Is this system directly connected to the internet?", "yes");
my $host_tcp_http2     = getTestParameter("NP_HOST_TCP_HTTP2", "A host providing an index page containing the string 'monitoring'", "test.monitoring-plugins.org");
my $host_tcp_proxy     = getTestParameter("NP_HOST_TCP_PROXY", "A host providing a HTTP proxy with CONNECT support", "localhost");
my $port_tcp_proxy     = getTestParameter("NP_PORT_TCP_PROXY", "Port of the proxy with HTTP and CONNECT support", "3128");

my $faketime = -x '/usr/bin/faketime' ? 1 : 0;


$res = NPTest->testCmd(
	"./$plugin $host_tcp_http -wt 300 -ct 600"
	);
cmp_ok( $res->return_code, '==', 0, "Webserver $host_tcp_http responded" );
like( $res->output, $successOutput, "Output OK" );

$res = NPTest->testCmd(
	"./$plugin $host_tcp_http -wt 300 -ct 600 -v -v -v -k 'bob:there' -k 'carl:frown'"
	);
like( $res->output, '/bob:there\r\ncarl:frown\r\n/', "Got headers with multiple -k options" );

$res = NPTest->testCmd(
	"./$plugin $host_nonresponsive -wt 1 -ct 2 -t 3"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $host_nonresponsive not responding" );
cmp_ok( $res->output, 'eq', "CRITICAL - Socket timeout after 3 seconds", "Output OK");

$res = NPTest->testCmd(
	"./$plugin $hostname_invalid -wt 1 -ct 2"
	);
cmp_ok( $res->return_code, '==', 2, "Webserver $hostname_invalid not valid" );
# The first part of the message comes from the OS catalogue, so cannot check this.
# On Debian, it is Name or service not known, on Darwin, it is No address associated with nodename
# Is also possible to get a socket timeout if DNS is not responding fast enough
like( $res->output, "/Unable to open TCP socket|Socket timeout after/", "Output OK");

# host header checks
$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );

SKIP: {
        skip "No internet access", 3 if $internet_access eq "no";

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http -S");
        like( $res->output, '/^Host: '.$host_tls_http.'\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http:8080 -S -p 443");
        like( $res->output, '/^Host: '.$host_tls_http.':8080\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http:443 -S -p 443");
        like( $res->output, '/^Host: '.$host_tls_http.'\s*$/ms', "Host Header OK" );
};

SKIP: {
        skip "No host serving monitoring in index file", 7 unless $host_tcp_http2;

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'monitoring'" );
        cmp_ok( $res->return_code, "==", 0, "Got a reference to 'monitoring'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 2, "Not got 'mONiTORing'");
        like ( $res->output, "/pattern not found/", "Error message says 'pattern not found'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -R 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 0, "But case insensitive doesn't mind 'mONiTORing'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'monitoring' --invert-regex" );
        cmp_ok( $res->return_code, "==", 2, "Invert results work when found");
        like ( $res->output, "/pattern found/", "Error message says 'pattern found'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'mONiTORing' --invert-regex" );
        cmp_ok( $res->return_code, "==", 0, "And also when not found");
}
SKIP: {
        skip "No internet access", 23 if $internet_access eq "no";

        $res = NPTest->testCmd(
                "./$plugin --ssl $host_tls_http"
                );
        cmp_ok( $res->return_code, '==', 0, "Can read https for $host_tls_http" );

        $res = NPTest->testCmd( "./$plugin -C 1 --ssl $host_tls_http" );
        cmp_ok( $res->return_code, '==', 0, "Checking certificate for $host_tls_http");
        like  ( $res->output, "/Certificate '$host_tls_cert' will expire on/", "Output OK" );
        my $saved_cert_output = $res->output;

        $res = NPTest->testCmd( "./$plugin -C 8000,1 --ssl $host_tls_http" );
        cmp_ok( $res->return_code, '==', 1, "Checking certificate for $host_tls_http");
        like  ( $res->output, qr/WARNING - Certificate '$host_tls_cert' expires in \d+ day/, "Output Warning" );

        $res = NPTest->testCmd( "./$plugin $host_tls_http -C 1" );
        is( $res->return_code, 0, "Old syntax for cert checking okay" );
        is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./$plugin -H $host_tls_http -C 1" );
        is( $res->return_code, 0, "Updated syntax for cert checking okay" );
        is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./$plugin -C 1 $host_tls_http" );
        cmp_ok( $res->output, 'eq', $saved_cert_output, "--ssl option automatically added");

        $res = NPTest->testCmd( "./$plugin $host_tls_http -C 1" );
        cmp_ok( $res->output, 'eq', $saved_cert_output, "Old syntax for cert checking still works");

        # run some certificate checks with faketime
        SKIP: {
                skip "No faketime binary found", 7 if !$faketime;
                $res = NPTest->testCmd("LC_TIME=C TZ=UTC ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/OK - Certificate '$host_tls_cert' will expire on/, "Catch cert output");
                is( $res->return_code, 0, "Catch cert output exit code" );
                my($mon,$day,$hour,$min,$sec,$year) = ($res->output =~ /(\w+)\s+(\d+)\s+(\d+):(\d+):(\d+)\s+(\d+)/);
                if(!defined $year) {
                    die("parsing date failed from: ".$res->output);
                }
                my $months = {'Jan' => 0, 'Feb' => 1, 'Mar' => 2, 'Apr' => 3, 'May' => 4, 'Jun' => 5, 'Jul' => 6, 'Aug' => 7, 'Sep' => 8, 'Oct' => 9, 'Nov' => 10, 'Dec' => 11};
                my $ts   = mktime($sec, $min, $hour, $day, $months->{$mon}, $year-1900);
                my $time = strftime("%Y-%m-%d %H:%M:%S", localtime($ts));
                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/CRITICAL - Certificate '$host_tls_cert' just expired/, "Output on expire date");

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-1))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/CRITICAL - Certificate '$host_tls_cert' expires in 0 minutes/, "cert expires in 1 second output");

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-120))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/CRITICAL - Certificate '$host_tls_cert' expires in 2 minutes/, "cert expires in 2 minutes output");

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-7200))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/CRITICAL - Certificate '$host_tls_cert' expires in 2 hours/, "cert expires in 2 hours output");

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/CRITICAL - Certificate '$host_tls_cert' expired on/, "Certificate expired output");
        };

        $res = NPTest->testCmd( "./$plugin --ssl $host_tls_http -E" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
        like  ( $res->output, '/time_ssl=[\d\.]+/', 'Extended Performance Data SSL Output OK' );

        $res = NPTest->testCmd( "./$plugin -H www.mozilla.com -u /firefox -f follow" );
        is( $res->return_code, 0, "Redirection based on location is okay");

        $res = NPTest->testCmd( "./$plugin -H www.mozilla.com --extended-perfdata" );
        like  ( $res->output, '/time_connect=[\d\.]+/', 'Extended Performance Data Output OK' );
}

SKIP: {
        skip "No internet access or proxy configured", 6 if $internet_access eq "no" or ! $host_tcp_proxy;

        $res = NPTest->testCmd( "./$plugin -I $host_tcp_proxy -p $port_tcp_proxy -u http://$host_tcp_http -e 200,301,302");
        is( $res->return_code, 0, "Proxy HTTP works");
        like($res->output, qr/OK: Status line output matched/, "Proxy HTTP Output is sufficent");

        $res = NPTest->testCmd( "./$plugin -I $host_tcp_proxy -p $port_tcp_proxy -H $host_tls_http -S -j CONNECT");
        is( $res->return_code, 0, "Proxy HTTP CONNECT works");
        like($res->output, qr/HTTP OK:/, "Proxy HTTP CONNECT output sufficent");

        $res = NPTest->testCmd( "./$plugin -I $host_tcp_proxy -p $port_tcp_proxy -H $host_tls_http -S -j CONNECT:HEAD");
        is( $res->return_code, 0, "Proxy HTTP CONNECT works with override method");
        like($res->output, qr/HTTP OK:/, "Proxy HTTP CONNECT output sufficent");
}
