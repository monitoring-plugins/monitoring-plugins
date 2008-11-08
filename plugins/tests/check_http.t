#! /usr/bin/perl -w -I ..
#
# Test check_http by having an actual HTTP server running
#

use strict;
use Test::More;
use NPTest;
use FindBin qw($Bin);

use HTTP::Daemon;
use HTTP::Status;
use HTTP::Response;

# set a fixed version, so the header size doesn't vary
$HTTP::Daemon::VERSION = "1.00";

my $port = 50000 + int(rand(1000));

my $pid = fork();
if ($pid) {
	# Parent
	#print "parent\n";
	# give our webserver some time to startup
	sleep(1);
} else {
	# Child
	#print "child\n";

	my $d = HTTP::Daemon->new(
		LocalPort => $port,
		LocalAddr => "127.0.0.1",
	) || die;
	print "Please contact me at: <URL:", $d->url, ">\n";
	while (my $c = $d->accept ) {
		while (my $r = $c->get_request) {
			if ($r->method eq "GET" and $r->url->path =~ m^/statuscode/(\d+)^) {
				$c->send_basic_header($1);
				$c->send_crlf;
			} elsif ($r->method eq "GET" and $r->url->path =~ m^/file/(.*)^) {
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_file_response("$Bin/var/$1");
			} elsif ($r->method eq "GET" and $r->url->path eq "/slow") {
				$c->send_basic_header;
				$c->send_crlf;
				sleep 1;
				$c->send_response("slow");
			} elsif ($r->url->path eq "/method") {
				if ($r->method eq "DELETE") {
					$c->send_error(RC_METHOD_NOT_ALLOWED);
				} elsif ($r->method eq "foo") {
					$c->send_error(RC_NOT_IMPLEMENTED);
				} else {
					$c->send_status_line(200, $r->method);
				}
			} elsif ($r->url->path eq "/postdata") {
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_response($r->method.":".$r->content);
			} elsif ($r->url->path eq "/redirect") {
				$c->send_redirect( "/redirect2" );
			} elsif ($r->url->path eq "/redirect2") {
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_response("redirected");
			} else {
				$c->send_error(RC_FORBIDDEN);
			}
			$c->close;
		}
	}
	exit;
}

END { if ($pid) { print "Killing $pid\n"; kill "INT", $pid } };

if ($ARGV[0] && $ARGV[0] eq "-d") {
	sleep 1000;
}

if (-x "./check_http") {
	plan tests => 47;
} else {
	plan skip_all => "No check_http compiled";
}

my $result;
my $command = "./check_http -H 127.0.0.1 -p $port";

$result = NPTest->testCmd( "$command -u /file/root" );
is( $result->return_code, 0, "/file/root");
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 274 bytes in [\d\.]+ seconds/', "Output correct" );

$result = NPTest->testCmd( "$command -u /file/root -s Root" );
is( $result->return_code, 0, "/file/root search for string");
TODO: {
local $TODO = "Output is different if a string is requested - should this be right?";
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 274 bytes in [\d\.]+ seconds/', "Output correct" );
}

$result = NPTest->testCmd( "$command -u /slow" );
is( $result->return_code, 0, "/file/root");
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 177 bytes in ([\d\.]+) seconds/', "Output correct" );
$result->output =~ /in ([\d\.]+) seconds/;
cmp_ok( $1, ">", 1, "Time is > 1 second" );

my $cmd;
$cmd = "$command -u /statuscode/200 -e 200";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 89 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -u /statuscode/201 -e 201";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 201 Created - 94 bytes in ([\d\.]+) seconds /', "Output correct: ".$result->output );

$cmd = "$command -u /statuscode/201 -e 200";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 2, $cmd);
like( $result->output, '/^HTTP CRITICAL - Invalid HTTP response received from host on port \d+: HTTP/1.1 201 Created/', "Output correct: ".$result->output );

$cmd = "$command -u /statuscode/200 -e 200,201,202";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 89 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -u /statuscode/201 -e 200,201,202";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 201 Created - 94 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -u /statuscode/203 -e 200,201,202";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 2, $cmd);
like( $result->output, '/^HTTP CRITICAL - Invalid HTTP response received from host on port (\d+): HTTP/1.1 203 Non-Authoritative Information/', "Output correct: ".$result->output );

$cmd = "$command -j HEAD -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 HEAD - 19 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -j POST -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 POST - 19 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -j GET -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 GET - 18 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 GET - 18 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -P foo -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 POST - 19 bytes in ([\d\.]+) seconds/', "Output correct: ".$result->output );

$cmd = "$command -j DELETE -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 1, $cmd);
like( $result->output, '/^HTTP WARNING: HTTP/1.1 405 Method Not Allowed/', "Output correct: ".$result->output );

$cmd = "$command -j foo -u /method";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 2, $cmd);
like( $result->output, '/^HTTP CRITICAL: HTTP/1.1 501 Not Implemented/', "Output correct: ".$result->output );

$cmd = "$command -P stufftoinclude -u /postdata -s POST:stufftoinclude";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - ([\d\.]+) second/', "Output correct: ".$result->output );

$cmd = "$command -j PUT -P stufftoinclude -u /postdata -s PUT:stufftoinclude";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - ([\d\.]+) second/', "Output correct: ".$result->output );

# To confirm that the free doesn't segfault
$cmd = "$command -P stufftoinclude -j PUT -u /postdata -s PUT:stufftoinclude";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - ([\d\.]+) second/', "Output correct: ".$result->output );

$cmd = "$command -u /redirect";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK - HTTP/1.1 301 Moved Permanently - [\d\.]+ second/', "Output correct: ".$result->output );

$cmd = "$command -f follow -u /redirect";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 183 bytes in [\d\.]+ second/', "Output correct: ".$result->output );

$cmd = "$command -u /redirect -k 'follow: me'";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK - HTTP/1.1 301 Moved Permanently - [\d\.]+ second/', "Output correct: ".$result->output );

$cmd = "$command -f follow -u /redirect -k 'follow: me'";
$result = NPTest->testCmd( $cmd );
is( $result->return_code, 0, $cmd);
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 183 bytes in [\d\.]+ second/', "Output correct: ".$result->output );

