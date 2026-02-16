#! /usr/bin/perl -w -I ..
#
# HyperText Transfer Protocol (HTTP) Test via check_curl
#
#

use strict;
use Test::More;
use POSIX qw/mktime strftime/;

use vars qw($tests $has_ipv6);

BEGIN {
    use NPTest;
    $has_ipv6 = NPTest::has_ipv6();
    $tests = $has_ipv6 ? 55 : 53;
    plan tests => $tests;
}


my $successOutput = '/.*HTTP.*second/';

my $res;
my $plugin = 'check_http';
$plugin    = 'check_curl' if $0 =~ m/check_curl/mx;

my $host_tcp_http      = getTestParameter("NP_HOST_TCP_HTTP", "A host providing the HTTP Service (a web server)", "localhost");
my $host_tcp_http_subdomain = getTestParameter("NP_HOST_TCP_HTTP_SUBDOMAIN", "A host that is served under a subdomain name", "subdomain1.localhost.com");
my $host_tcp_http_ipv4 = getTestParameter("NP_HOST_TCP_HTTP_IPV4", "An IPv6 address providing a HTTP Service (a web server)", "127.0.0.1");
my $host_tcp_http_ipv4_cidr_1 = getTestParameter("NP_HOST_TCP_HTTP_IPV4_CIDR_1", "A CIDR that the provided IPv4 address is in.");
my $host_tcp_http_ipv4_cidr_2 = getTestParameter("NP_HOST_TCP_HTTP_IPV4_CIDR_2", "A CIDR that the provided IPv4 address is in.");
my $host_tcp_http_ipv6      = getTestParameter("NP_HOST_TCP_HTTP_IPV6", "An IPv6 address providing a HTTP Service (a web server)", "::1");
my $host_tcp_http_ipv6_cidr_1 = getTestParameter("NP_HOST_TCP_HTTP_IPV6_CIDR_1", "A CIDR that the provided IPv6 address is in.");
my $host_tcp_http_ipv6_cidr_2 = getTestParameter("NP_HOST_TCP_HTTP_IPV6_CIDR_2", "A CIDR that the provided IPv6 address is in.");
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

if ($has_ipv6) {
    # Test for IPv6 formatting
    $res = NPTest->testCmd(
        "./$plugin -I $host_tcp_http_ipv6 -wt 300 -ct 600"
        );
    cmp_ok( $res->return_code, '==', 0, "IPv6 URL formatting is working" );
    like( $res->output, $successOutput, "Output OK" );
}

$res = NPTest->testCmd(
    "./$plugin $host_tcp_http -wt 300 -ct 600 -v -v -v -k 'bob:there' -k 'carl:frown'"
    );
like( $res->output, '/bob:there\r\ncarl:frown\r\n/', "Got headers with multiple -k options" );

$res = NPTest->testCmd(
    "./$plugin $host_nonresponsive -wt 1 -ct 2 -t 3"
    );
cmp_ok( $res->return_code, '==', 2, "Webserver $host_nonresponsive not responding" );
# was CRITICAL only, but both check_curl and check_http print HTTP CRITICAL (puzzle?!)
like( $res->output, "/cURL returned 28 - Connection timed out after/", "Output OK");

$res = NPTest->testCmd(
    "./$plugin $hostname_invalid -wt 1 -ct 2"
    );
cmp_ok( $res->return_code, '==', 2, "Webserver $hostname_invalid not valid" );
# The first part of the message comes from the OS catalogue, so cannot check this.
# On Debian, it is Name or service not known, on Darwin, it is No address associated with nodename
# Is also possible to get a socket timeout if DNS is not responding fast enough
# cURL gives us consistent strings from it's own 'lib/strerror.c'
like( $res->output, "/cURL returned 6 - Could not resolve host:/", "Output OK");

# host header checks
$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.'\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http:8080 -p 80");
like( $res->output, '/^Host: '.$host_tcp_http.':8080\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

$res = NPTest->testCmd("./$plugin -v -H $host_tcp_http:8080 -p 80 -k 'Host: testhost:8001'");
like( $res->output, '/^Host: testhost:8001\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

$res = NPTest->testCmd("./$plugin -v -I $host_tcp_http -p 80 -k 'Host: testhost:8001'");
like( $res->output, '/^Host: testhost:8001\s*$/ms', "Host Header OK" );
like( $res->output, '/CURLOPT_URL: http:\/\/'.$host_tcp_http.':80\//ms', "Url OK" );

SKIP: {
        skip "No internet access", 4 if $internet_access eq "no";

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http -S");
        like( $res->output, '/^Host: '.$host_tls_http.'\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http:8080 -S -p 443");
        like( $res->output, '/^Host: '.$host_tls_http.':8080\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http:443 -S -p 443");
        like( $res->output, '/^Host: '.$host_tls_http.'\s*$/ms', "Host Header OK" );

        $res = NPTest->testCmd("./$plugin -v -H $host_tls_http -D -S -p 443");
        like( $res->output, '/(^Host: '.$host_tls_http.'\s*$)|(cURL returned 60)/ms', "Host Header OK" );
};

SKIP: {
        skip "No host serving monitoring in index file", 7 unless $host_tcp_http2;

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'monitoring'" );
        cmp_ok( $res->return_code, "==", 0, "Got a reference to 'monitoring'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 2, "Not got 'mONiTORing'");
        like ( $res->output, "/matched not/", "Error message says 'matched not'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -R 'mONiTORing'" );
        cmp_ok( $res->return_code, "==", 0, "But case insensitive doesn't mind 'mONiTORing'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'monitoring' --invert-regex" );
        cmp_ok( $res->return_code, "==", 2, "Invert results work when found");
        like ( $res->output, "/matched/", "Error message says 'matched'");

        $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 -r 'mONiTORing' --invert-regex" );
        cmp_ok( $res->return_code, "==", 0, "And also when not found");
}
SKIP: {
        skip "No internet access", 28 if $internet_access eq "no";

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
        like  ( $res->output, qr/Certificate '$host_tls_cert' expires in \d+ day/, "Output Warning" );

        $res = NPTest->testCmd( "./$plugin $host_tls_http -C 1" );
        is( $res->return_code, 0, "Old syntax for cert checking okay" );
        # deactivated since different timings will change the output
        # TODO compare without perfdata
        # is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./$plugin -H $host_tls_http -C 1" );
        is( $res->return_code, 0, "Updated syntax for cert checking okay" );
        # deactivated since different timings will change the output
        # TODO compare without perfdata
        # is( $res->output, $saved_cert_output, "Same output as new syntax" );

        $res = NPTest->testCmd( "./$plugin -C 1 $host_tls_http" );
        # deactivated since different timings will change the output
        # TODO compare without perfdata
        # cmp_ok( $res->output, 'eq', $saved_cert_output, "--ssl option automatically added");

        $res = NPTest->testCmd( "./$plugin $host_tls_http -C 1" );
        # deactivated since different timings will change the output
        # TODO compare without perfdata
        # cmp_ok( $res->output, 'eq', $saved_cert_output, "Old syntax for cert checking still works");

        # run some certificate checks with faketime
        SKIP: {
                skip "No faketime binary found", 12 if !$faketime;
                $res = NPTest->testCmd("LC_TIME=C TZ=UTC ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' will expire on/, "Catch cert output");
                is( $res->return_code, 0, "Catch cert output exit code" );

                my($mon,$day,$hour,$min,$sec,$year) = ($res->output =~ /(\w+)\s+(\d+)\s+(\d+):(\d+):(\d+)\s+(\d+)/);
                if(!defined $year) {
                    die("parsing date failed from: ".$res->output);
                }

                my $months = {'Jan' => 0, 'Feb' => 1, 'Mar' => 2, 'Apr' => 3, 'May' => 4, 'Jun' => 5, 'Jul' => 6, 'Aug' => 7, 'Sep' => 8, 'Oct' => 9, 'Nov' => 10, 'Dec' => 11};
                my $ts   = mktime($sec, $min, $hour, $day, $months->{$mon}, $year-1900);
                my $time = strftime("%Y-%m-%d %H:%M:%S", localtime($ts));

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' just expired/, "Output on expire date");
                is( $res->return_code, 2, "Output on expire date" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-1))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' expires in 0 minutes/, "cert expires in 1 second output");
                is( $res->return_code, 2, "cert expires in 1 second exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-120))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' expires in 2 minutes/, "cert expires in 2 minutes output");
                is( $res->return_code, 2, "cert expires in 2 minutes exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts-7200))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' expires in 2 hours/, "cert expires in 2 hours output");
                is( $res->return_code, 2, "cert expires in 2 hours exit code" );

                $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./$plugin -C 1 $host_tls_http");
                like($res->output, qr/Certificate '$host_tls_cert' expired on/, "Certificate expired output");
                is( $res->return_code, 2, "Certificate expired exit code" );
        };

        $res = NPTest->testCmd( "./$plugin --ssl $host_tls_http -E" );
        like  ( $res->output, '/\'time_connect\'=[\d\.]+/', 'Extended Performance Data Output OK' );
        like  ( $res->output, '/\'time_tls\'=[\d\.]+/', 'Extended Performance Data SSL Output OK' );

        $res = NPTest->testCmd( "./$plugin -H monitoring-plugins.org -u /download.html -f follow" );
        is( $res->return_code, 0, "Redirection based on location is okay");

        $res = NPTest->testCmd( "./$plugin -H monitoring-plugins.org --extended-perfdata" );
        like  ( $res->output, '/\'time_connect\'=[\d\.]+/', 'Extended Performance Data Output OK' );
}
SKIP: {
    skip "No internet access", 2 if $internet_access eq "no";

    # Proxy tests
    # These are the proxy tests that require a working proxy server
    # The debian container in the github workflow runs a squid proxy server at port 3128
    # Test that dont require one, like argument/environment variable parsing are in plugins/tests/check_curl.t

    # Test if proxy works
    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv4 --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http_ipv4 works" );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv6 --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http_ipv6 works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http2 --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http2 works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http_subdomain --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http_subdomain works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tls_http --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, there are no preventative measures ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tls_http works" );

    # Noproxy '*' should prevent using proxy in any setting, even if its specified
    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http_subdomain --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy \"\*\" -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since noproxy has \"\*\" ");
    is( $res->return_code, 0, "Should reach $host_tcp_http_subdomain with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv4 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy \"\*\" -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since noproxy has \"\*\" ");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv4 with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv6 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy \"\*\" -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since noproxy has \"\*\" ");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv6 with or without proxy." );

    # Noproxy domain should prevent using proxy for subdomains of that domain 
    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http_subdomain --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since subdomain: $host_tcp_http_subdomain  is under a noproxy domain: $host_tcp_http");
    is( $res->return_code, 0, "Should reach $host_tcp_http_subdomain with or without proxy." );

    # Noproxy should prevent using IP matches if an IP is found directly
    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv4 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv4 -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv4 is added into noproxy: $host_tcp_http_ipv4");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv4 with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv6 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv6 -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv6 is added into noproxy: $host_tcp_http_ipv6");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv6 with or without proxy." );

    # Noproxy should prevent using IP matches if a CIDR region that contains that Ip is used directly.
    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv4 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv4_cidr_1 -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv4 is inside CIDR range: $host_tcp_http_ipv4_cidr_1");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv4 with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv4 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv4_cidr_2 -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv4 is inside CIDR range: $host_tcp_http_ipv4_cidr_2");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv4 with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv6 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv6_cidr_1 -v " );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv6 is inside CIDR range: $host_tcp_http_ipv6_cidr_1");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv6 with or without proxy." );

    $res = NPTest->testCmd( "./$plugin -I $host_tcp_http_ipv6 --proxy http://$host_tcp_proxy:$port_tcp_proxy --noproxy $host_tcp_http_ipv6_cidr_2 -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is not used since IP address: $host_tcp_http_ipv6 is inside CIDR range: $host_tcp_http_ipv6_cidr_2");
    is( $res->return_code, 0, "Should reach $host_tcp_http_ipv6 with or without proxy." );

    # Noproxy should discern over different types of proxy schemes
    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy http://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, and is using scheme http ");
    is( $res->return_code, 0, "Using proxy http:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy https://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, and is using scheme https");
    is( $res->return_code, 0, "Using proxy https:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy socks4://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is used, and is using scheme socks4");
    is( $res->return_code, 0, "Using proxy socks4:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy socks4a://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 1/m, "proxy is used, and is using scheme socks4a");
    is( $res->return_code, 0, "Using proxy socks4a:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy socks5://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is used, and is using scheme socks5");
    is( $res->return_code, 0, "Using proxy socks5:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );

    $res = NPTest->testCmd( "./$plugin -H $host_tcp_http --proxy socks5h://$host_tcp_proxy:$port_tcp_proxy -v" );
    like($res->output, qr/^\* proxy_resolves_hostname: 0/m, "proxy is used, and is using scheme socks5h");
    is( $res->return_code, 0, "Using proxy socks5h:$host_tcp_proxy:$port_tcp_proxy to connect to $host_tcp_http works" );
}
