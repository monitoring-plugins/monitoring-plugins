#! /usr/bin/perl -w -I ..
#
# Test check_http by having an actual HTTP server running
#
# To create the https server certificate:
# openssl req -new -x509 -keyout server-key.pem -out server-cert.pem -days 3650 -nodes
# Country Name (2 letter code) [AU]:UK
# State or Province Name (full name) [Some-State]:Derbyshire
# Locality Name (eg, city) []:Belper
# Organization Name (eg, company) [Internet Widgits Pty Ltd]:Nagios Plugins
# Organizational Unit Name (eg, section) []:
# Common Name (eg, YOUR name) []:Ton Voon
# Email Address []:tonvoon@mac.com

use strict;
use Test::More;
use NPTest;
use FindBin qw($Bin);

my $common_tests = 70;
my $ssl_only_tests = 8;
# Check that all dependent modules are available
eval {
	require HTTP::Daemon;
	require HTTP::Status;
	require HTTP::Response;
};

if ($@) {
	plan skip_all => "Missing required module for test: $@";
} else {
	if (-x "./check_http") {
		plan tests => $common_tests * 2 + $ssl_only_tests;
	} else {
		plan skip_all => "No check_http compiled";
	}
}

my $servers = { http => 0 };	# HTTP::Daemon should always be available
eval { require HTTP::Daemon::SSL };
if ($@) {
	diag "Cannot load HTTP::Daemon::SSL: $@";
} else {
	$servers->{https} = 0;
}

# set a fixed version, so the header size doesn't vary
$HTTP::Daemon::VERSION = "1.00";

my $port_http = 50000 + int(rand(1000));
my $port_https = $port_http + 1;
my $port_https_expired = $port_http + 2;

# This array keeps sockets around for implementing timeouts
my @persist;

# Start up all servers
my @pids;
my $pid = fork();
if ($pid) {
	# Parent
	push @pids, $pid;
	if (exists $servers->{https}) {
		# Fork a normal HTTPS server
		$pid = fork();
		if ($pid) {
			# Parent
			push @pids, $pid;
			# Fork an expired cert server
			$pid = fork();
			if ($pid) {
				push @pids, $pid;
			} else {
				my $d = HTTP::Daemon::SSL->new(
					LocalPort => $port_https_expired,
					LocalAddr => "127.0.0.1",
					SSL_cert_file => "$Bin/certs/expired-cert.pem",
					SSL_key_file => "$Bin/certs/expired-key.pem",
				) || die;
				print "Please contact https expired at: <URL:", $d->url, ">\n";
				run_server( $d );
				exit;
			}
		} else {
			my $d = HTTP::Daemon::SSL->new(
				LocalPort => $port_https,
				LocalAddr => "127.0.0.1",
				SSL_cert_file => "$Bin/certs/server-cert.pem",
				SSL_key_file => "$Bin/certs/server-key.pem",
			) || die;
			print "Please contact https at: <URL:", $d->url, ">\n";
			run_server( $d );
			exit;
		}
	}
	# give our webservers some time to startup
	sleep(1);
} else {
	# Child
	#print "child\n";
	my $d = HTTP::Daemon->new(
		LocalPort => $port_http,
		LocalAddr => "127.0.0.1",
	) || die;
	print "Please contact http at: <URL:", $d->url, ">\n";
	run_server( $d );
	exit;
}

# Run the same server on http and https
sub run_server {
	my $d = shift;
	MAINLOOP: while (my $c = $d->accept ) {
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
					$c->send_error(HTTP::Status->RC_METHOD_NOT_ALLOWED);
				} elsif ($r->method eq "foo") {
					$c->send_error(HTTP::Status->RC_NOT_IMPLEMENTED);
				} else {
					$c->send_status_line(200, $r->method);
				}
			} elsif ($r->url->path eq "/postdata") {
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_response($r->method.":".$r->content);
			} elsif ($r->url->path eq "/redirect") {
				$c->send_redirect( "/redirect2" );
			} elsif ($r->url->path eq "/redir_external") {
				$c->send_redirect(($d->isa('HTTP::Daemon::SSL') ? "https" : "http") . "://169.254.169.254/redirect2" );
			} elsif ($r->url->path eq "/redirect2") {
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_response(HTTP::Response->new( 200, 'OK', undef, 'redirected' ));
			} elsif ($r->url->path eq "/redir_timeout") {
				$c->send_redirect( "/timeout" );
			} elsif ($r->url->path eq "/timeout") {
				# Keep $c from being destroyed, but prevent severe leaks
				unshift @persist, $c;
				delete($persist[1000]);
				next MAINLOOP;
			} elsif ($r->url->path eq "/header_check") {
				$c->send_basic_header;
				$c->send_header('foo');
				$c->send_crlf;
			} else {
				$c->send_error(HTTP::Status->RC_FORBIDDEN);
			}
			$c->close;
		}
	}
}

END {
	foreach my $pid (@pids) {
		if ($pid) { print "Killing $pid\n"; kill "INT", $pid }
	}
};

if ($ARGV[0] && $ARGV[0] eq "-d") {
	while (1) {
		sleep 100;
	}
}

my $result;
my $command = "./check_http -H 127.0.0.1";

run_common_tests( { command => "$command -p $port_http" } );
SKIP: {
	skip "HTTP::Daemon::SSL not installed", $common_tests + $ssl_only_tests if ! exists $servers->{https};
	run_common_tests( { command => "$command -p $port_https", ssl => 1 } );

	$result = NPTest->testCmd( "$command -p $port_https -S -C 14" );
	is( $result->return_code, 0, "$command -p $port_https -S -C 14" );
	is( $result->output, 'OK - Certificate \'Ton Voon\' will expire on 03/03/2019 21:41.', "output ok" );

	$result = NPTest->testCmd( "$command -p $port_https -S -C 14000" );
	is( $result->return_code, 1, "$command -p $port_https -S -C 14000" );
	like( $result->output, '/WARNING - Certificate \'Ton Voon\' expires in \d+ day\(s\) \(03/03/2019 21:41\)./', "output ok" );

	# Expired cert tests
	$result = NPTest->testCmd( "$command -p $port_https -S -C 13960,14000" );
	is( $result->return_code, 2, "$command -p $port_https -S -C 13960,14000" );
	like( $result->output, '/CRITICAL - Certificate \'Ton Voon\' expires in \d+ day\(s\) \(03/03/2019 21:41\)./', "output ok" );

	$result = NPTest->testCmd( "$command -p $port_https_expired -S -C 7" );
	is( $result->return_code, 2, "$command -p $port_https_expired -S -C 7" );
	is( $result->output,
		'CRITICAL - Certificate \'Ton Voon\' expired on 03/05/2009 00:13.',
		"output ok" );

}

sub run_common_tests {
	my ($opts) = @_;
	my $command = $opts->{command};
	if ($opts->{ssl}) {
		$command .= " --ssl";
	}

	$result = NPTest->testCmd( "$command -u /file/root" );
	is( $result->return_code, 0, "/file/root");
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - 274 bytes in [\d\.]+ second/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /file/root -s Root" );
	is( $result->return_code, 0, "/file/root search for string");
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - 274 bytes in [\d\.]+ second/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /file/root -s NonRoot" );
	is( $result->return_code, 2, "Missing string check");
	like( $result->output, qr%^HTTP CRITICAL: HTTP/1\.1 200 OK - string 'NonRoot' not found on 'https?://127\.0\.0\.1:\d+/file/root'%, "Shows search string and location");

	$result = NPTest->testCmd( "$command -u /file/root -s NonRootWithOver30charsAndMoreFunThanAWetFish" );
	is( $result->return_code, 2, "Missing string check");
	like( $result->output, qr%HTTP CRITICAL: HTTP/1\.1 200 OK - string 'NonRootWithOver30charsAndM...' not found on 'https?://127\.0\.0\.1:\d+/file/root'%, "Shows search string and location");

	$result = NPTest->testCmd( "$command -u /header_check -d foo" );
	is( $result->return_code, 0, "header_check search for string");
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - 96 bytes in [\d\.]+ second/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /header_check -d bar" );
	is( $result->return_code, 2, "Missing header string check");
	like( $result->output, qr%^HTTP CRITICAL: HTTP/1\.1 200 OK - header 'bar' not found on 'https?://127\.0\.0\.1:\d+/header_check'%, "Shows search string and location");

	my $cmd;
	$cmd = "$command -u /slow";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, "$cmd");
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );
	$result->output =~ /in ([\d\.]+) second/;
	cmp_ok( $1, ">", 1, "Time is > 1 second" );

	$cmd = "$command -u /statuscode/200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/200 -e 200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: Status line output matched "200" - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 201 Created - \d+ bytes in [\d\.]+ second /', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 201";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: Status line output matched "201" - \d+ bytes in [\d\.]+ second /', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 2, $cmd);
	like( $result->output, '/^HTTP CRITICAL - Invalid HTTP response received from host on port \d+: HTTP/1.1 201 Created/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/200 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: Status line output matched "200,201,202" - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: Status line output matched "200,201,202" - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/203 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 2, $cmd);
	like( $result->output, '/^HTTP CRITICAL - Invalid HTTP response received from host on port (\d+): HTTP/1.1 203 Non-Authoritative Information/', "Output correct: ".$result->output );

	$cmd = "$command -j HEAD -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 HEAD - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -j POST -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 POST - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -j GET -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 GET - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 GET - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -P foo -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 POST - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

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
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -j PUT -P stufftoinclude -u /postdata -s PUT:stufftoinclude";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	# To confirm that the free doesn't segfault
	$cmd = "$command -P stufftoinclude -j PUT -u /postdata -s PUT:stufftoinclude";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /redirect";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 301 Moved Permanently - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -f follow -u /redirect";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 301 Moved Permanently - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -f follow -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -f sticky -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

	$cmd = "$command -f stickyport -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/^HTTP OK: HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second/', "Output correct: ".$result->output );

  # These tests may block
	print "ALRM\n";

	# stickyport - on full urlS port is set back to 80 otherwise
	$cmd = "$command -f stickyport -u /redir_external -t 5 -s redirected";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(2);
		$result = NPTest->testCmd( $cmd );
		alarm(0);	};
	isnt( $@, "alarm\n", $cmd );
	is( $result->return_code, 0, $cmd );

	# Let's hope there won't be any web server on :80 returning "redirected"!
	$cmd = "$command -f sticky -u /redir_external -t 5 -s redirected";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(2);
		$result = NPTest->testCmd( $cmd );
		alarm(0); };
	isnt( $@, "alarm\n", $cmd );
	isnt( $result->return_code, 0, $cmd );

	# Test an external address - timeout
	SKIP: {
		skip "This doesn't seems to work all the time", 1 unless ($ENV{HTTP_EXTERNAL});
		$cmd = "$command -f follow -u /redir_external -t 5";
		eval {
			local $SIG{ALRM} = sub { die "alarm\n" };
			alarm(2);
			$result = NPTest->testCmd( $cmd );
			alarm(0); };
		is( $@, "alarm\n", $cmd );
	}

	$cmd = "$command -u /timeout -t 5";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(2);
		$result = NPTest->testCmd( $cmd );
		alarm(0); };
	is( $@, "alarm\n", $cmd );

	$cmd = "$command -f follow -u /redir_timeout -t 2";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(5);
		$result = NPTest->testCmd( $cmd );
		alarm(0); };
	isnt( $@, "alarm\n", $cmd );

}
