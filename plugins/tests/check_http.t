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
		LocalPort => $port
	) || die;
	print "Please contact me at: <URL:", $d->url, ">\n";
	while (my $c = $d->accept ) {
		while (my $r = $c->get_request) {
			if ($r->method eq "GET" and $r->url->path eq "/xyzzy") {
				$c->send_file_response("/etc/passwd");
			} elsif ($r->method eq "GET" and $r->url->path =~ m^/statuscode/(\d+)^) {
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
	plan tests => 19;
} else {
	plan skip_all => "No check_http compiled";
}

my $result;
my $command = "./check_http -H 127.0.0.1 -p $port";

$result = NPTest->testCmd( "$command -u /file/root" );
is( $result->return_code, 0, "/file/root");
like( $result->output, '/^HTTP OK HTTP/1.1 200 OK - 274 bytes in [\d\.]+ seconds/', "Output correct" );

TODO: {
local $TODO = "Output is different if a string is requested - should this be right?";
$result = NPTest->testCmd( "$command -u /file/root -s Root" );
is( $result->return_code, 0, "/file/root search for string");
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

