#! /usr/bin/perl -w -I ..
#
# Test check_http by having an actual HTTP server running
#
# To create the https server certificate:
# openssl req -new -x509 -keyout server-key.pem -out server-cert.pem -days 3650 -nodes
# to create a new expired certificate:
# faketime '2008-01-01 12:00:00' openssl req -new -x509 -keyout expired-key.pem -out expired-cert.pem -days 1 -nodes
# Country Name (2 letter code) [AU]:DE
# State or Province Name (full name) [Some-State]:Bavaria
# Locality Name (eg, city) []:Munich
# Organization Name (eg, company) [Internet Widgets Pty Ltd]:Monitoring Plugins
# Organizational Unit Name (eg, section) []:
# Common Name (e.g. server FQDN or YOUR name) []:Monitoring Plugins
# Email Address []:devel@monitoring-plugins.org

use strict;
use warnings;
use Test::More;
use NPTest;
use FindBin qw($Bin);

use URI;
use URI::QueryParam;
use HTTP::Daemon;
use HTTP::Daemon::SSL;

$ENV{'LC_TIME'} = "C";

my $common_tests = 115;
my $ssl_only_tests = 12;
# Check that all dependent modules are available
eval "use HTTP::Daemon 6.01;";
plan skip_all => 'HTTP::Daemon >= 6.01 required' if $@;
eval {
	require HTTP::Status;
	require HTTP::Response;
};

my $plugin = 'check_http';
$plugin    = 'check_curl' if $0 =~ m/check_curl/mx;

# look for libcurl version to see if some advanced checks are possible (>= 7.49.0)
my $advanced_checks = 16;
my $use_advanced_checks = 0;
my $required_version = '7.49.0';
my $virtual_host = 'www.somefunnyhost.com';
my $virtual_port = 42;
my $curl_version = '';
open (my $fh, '-|', "./$plugin --version") or die;
while (<$fh>) {
	if (m{libcurl/([\d.]+)\s}) {
		$curl_version = $1;
		last;
	}
}
close ($fh);
if ($curl_version) {
	my ($major, $minor, $release) = split (/\./, $curl_version);
	my ($req_major, $req_minor, $req_release) = split (/\./, $required_version);
	my $check = ($major <=> $req_major or $minor <=> $req_minor or $release <=> $req_release);
	if ($check >= 0) {
		$use_advanced_checks = 1;
		print "Found libcurl $major.$minor.$release. Using advanced checks\n";
	}
}

if ($@) {
	plan skip_all => "Missing required module for test: $@";
} else {
	if (-x "./$plugin") {
		plan tests => $common_tests * 2 + $ssl_only_tests + $advanced_checks;
	} else {
		plan skip_all => "No $plugin compiled";
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

# give our webservers some time to startup
sleep(3);

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
			} elsif ($r->url->path eq "/redirect_rel") {
				$c->send_basic_header(302);
				$c->send_header("Location", "/redirect2" );
				$c->send_crlf;
				$c->send_response('moved to /redirect2');
			} elsif ($r->url->path eq "/redir_timeout") {
				$c->send_redirect( "/timeout" );
            } elsif ($r->url->path =~ m{^/redirect_with_increment}) {
				# <scheme>://<username>:<password>@<host>:<port>/<path>;<parameters>?<query>#<fragment>
				# Find every parameter, query , and fragment keys and increment them

				my $content = "";

                # Use URI to help with query/fragment; parse path params manually.
                my $original_url = $r->url->as_string;
				$content .= " original_url: ${original_url}\n";
                my $uri = URI->new($original_url);
				$content .= " uri: ${uri}\n";

				my $path = $uri->path // '';
				my $query = $uri->query // '';
				my $fragment = $uri->fragment // '';

				$content .= " path: ${path}\n";
				$content .= " query: ${query}\n";
				$content .= " fragment: ${fragment}\n";

                # split the URI part and parameters. URI package cannot do this
                # group 1 is captured: anything without a semicolon: ([^;]*)
                # group 2 is uncaptured: (?:;(.*))?
                # (?: ... )? prevents capturing the parameter section
				# inside group 2, ';' matches the first ever semicolon
				# group3 is captured: any character string : (.*)
				# \? matches an actual ? mark, which starts the query parameters
                my ($before_params, $params) = $uri =~ m{^([^;]*)(?:;(.*))?\?};
				$before_params //= '';
				$params //= '';
				$content .= " before_params: ${before_params}\n";
				$content .= " params: ${params}\n";
                my @parameter_pairs;
                if (defined $params && length $params) {
                    for my $p (split /;/, $params) {
                        my ($key,$value) = split /=/, $p, 2;
                        $value //= '';
                        push @parameter_pairs, [ $key, $value ];
						$content .= " parameter: ${key} -> ${value}\n";
                    }
                }

                # query parameters are offered directly from the library
                my @query_form = $uri->query_form;
                my @query_parameter_pairs;
                while (@query_form) {
                    my $key = shift @query_form;
                    my $value = shift @query_form;
					$value //= ''; # there can be valueless keys
                    push @query_parameter_pairs, [ $key, $value ];
					$content .= " query: ${key} -> ${value}\n";
                }

                # helper to increment value
                my $increment = sub {
                    my ($v) = @_;
                    return $v if !defined $v || $v eq '';
                    # numeric integer
                    if ($v =~ /^-?\d+$/) {
                        return $v + 1;
                    }
                    # otherwise -> increment as if its an ascii character
					# sed replacement syntax, but the $& holds the matched character
                    if (length($v)) {
                        (my $new_v = $v) =~ s/./chr(ord($&) + 1)/ge;
        				return $new_v;
                    }
                };

                # increment values in pairs
                for my $pair (@parameter_pairs) {
                    $pair->[1] = $increment->($pair->[1]);
					$content .= " parameter new: " . $pair->[0] . " -> " . $pair->[1] . "\n";
                }
                for my $pair (@query_parameter_pairs) {
                    $pair->[1] = $increment->($pair->[1]);
					$content .= " query parameter new: " . $pair->[0] . " -> " . $pair->[1] . "\n";
                }

                # rebuild strings
                my $new_parameter_str = join(';', map { $_->[0] . '=' . $_->[1] } @parameter_pairs);
				$content .= " new_parameter_str: ${new_parameter_str}\n";

				# library can rebuild from an array
                my @new_query_form;
                for my $p (@query_parameter_pairs) { push @new_query_form, $p->[0], $p->[1] }

				my $new_fragment_str = '';
				for my $pair (@parameter_pairs) {
					my $key = $pair->[0];
					my $value = $pair->[1];
					if ($key eq "fragment") {
						$new_fragment_str = $value
					}
                }
				$content .= " new_fragment_str: ${new_fragment_str}\n";

                # construct new URI using the library
                my $new_uri = URI->new('');
                $new_uri->path( $before_params . ($new_parameter_str ? ';' . $new_parameter_str : '') );
                $new_uri->query_form( \@new_query_form ) if @new_query_form;
                $new_uri->fragment( $new_fragment_str ) if $new_fragment_str ne '';
				$content .= " new_uri: ${new_uri}\n";

				# Redirect until fail_count or redirect_count reaches 3
				if ($new_uri =~ /fail_count=3/){
					$c->send_error(HTTP::Status->RC_FORBIDDEN, "fail count reached 3, url path:" . $r->url->path );
				} elsif ($new_uri =~ /redirect_count=3/){
					$c->send_response(HTTP::Response->new( 200, 'OK', undef , $content ));
				} elsif ($new_uri =~ /location_redirect_count=3/){
					$c->send_basic_header(302);
					$c->send_header("Location", "$new_uri" );
					$c->send_crlf;
					$c->send_response("$content \n moved to $new_uri");
				} else {
                	$c->send_redirect( $new_uri->as_string, 301, $content );
				}
			} elsif ($r->url->path eq "/timeout") {
				# Keep $c from being destroyed, but prevent severe leaks
				unshift @persist, $c;
				delete($persist[1000]);
				next MAINLOOP;
			} elsif ($r->url->path eq "/header_check") {
				$c->send_basic_header;
				$c->send_header('foo');
				$c->send_crlf;
			} elsif ($r->url->path eq "/header_broken_check") {
				$c->send_basic_header;
				$c->send_header('foo');
				print $c "Test1:: broken\n";
				print $c " Test2: leading whitespace\n";
				$c->send_crlf;
			} elsif ($r->url->path eq "/virtual_port") {
				# return sent Host header
				$c->send_basic_header;
				$c->send_crlf;
				$c->send_response(HTTP::Response->new( 200, 'OK', undef, $r->header ('Host')));
			} elsif ($r->url->path eq "/chunked") {
				my $chunks = ["chunked", "encoding", "test\n"];
				$c->send_response(HTTP::Response->new( 200, 'OK', undef, sub {
					my $chunk = shift @{$chunks};
					return unless $chunk;
					sleep(1);
					return($chunk);
				}));
			} else {
				$c->send_error(HTTP::Status->RC_FORBIDDEN, "unknown url path:" . $r->url->path );
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
my $command = "./$plugin -H 127.0.0.1";

run_common_tests( { command => "$command -p $port_http" } );
SKIP: {
	skip "HTTP::Daemon::SSL not installed", $common_tests + $ssl_only_tests if ! exists $servers->{https};
	run_common_tests( { command => "$command -p $port_https", ssl => 1 } );

	my $expiry = "Thu Nov 28 21:02:11 2030 +0000";

	$result = NPTest->testCmd( "$command -p $port_https -S -C 14" );
	is( $result->return_code, 0, "$command -p $port_https -S -C 14" );
	like( $result->output, '/.*Certificate \'Monitoring Plugins\' will expire on ' . quotemeta($expiry) . '.*/', "output ok" );

	$result = NPTest->testCmd( "$command -p $port_https -S -C 14000" );
	is( $result->return_code, 1, "$command -p $port_https -S -C 14000" );
	like( $result->output, '/.*Certificate \'Monitoring Plugins\' expires in \d+ day\(s\) \(' . quotemeta($expiry) . '\).*/', "output ok" );

	# Expired cert tests
	$result = NPTest->testCmd( "$command -p $port_https -S -C 13960,14000" );
	is( $result->return_code, 2, "$command -p $port_https -S -C 13960,14000" );
	like( $result->output, '/.*Certificate \'Monitoring Plugins\' expires in \d+ day\(s\) \(' . quotemeta($expiry) . '\).*/', "output ok" );

	$result = NPTest->testCmd( "$command -p $port_https_expired -S -C 7" );
	is( $result->return_code, 2, "$command -p $port_https_expired -S -C 7" );
	like( $result->output,
		'/.*Certificate \'Monitoring Plugins\' expired on Wed Jan\s+2 12:00:00 2008 \+0000.*/',
		"output ok" );

}

my $cmd;

# advanced checks with virtual hostname and virtual port
SKIP: {
	skip "libcurl version is smaller than $required_version", 6 unless $use_advanced_checks;

	# http without virtual port
	$cmd = "./$plugin -H $virtual_host -I 127.0.0.1 -p $port_http -u /virtual_port -r ^$virtual_host:$port_http\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# http with virtual port (!= 80)
	$cmd = "./$plugin -H $virtual_host:$virtual_port -I 127.0.0.1 -p $port_http -u /virtual_port -r ^$virtual_host:$virtual_port\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# http with virtual port (80)
	$cmd = "./$plugin -H $virtual_host:80 -I 127.0.0.1 -p $port_http -u /virtual_port -r ^$virtual_host\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# curlopt proxy/noproxy parsing tests, ssl disabled
	{
		# Make a scope and change environment variables here, to not mess them up for other tests using environment variables

		# Test: Only environment variable 'http_proxy', should be picked up
		local $ENV{"http_proxy"} = 'http://proxy.example.com:8080';
		$cmd = "$command -u /statuscode/200 -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://proxy.example.com:8080 */', "Correctly took 'http_proxy' environment variable: ".$result->output );
		delete($ENV{"http_proxy"});

		# Test: Two environment variables, 'http_proxy' and 'HTTP_PROXY', lowercase should be used
		local $ENV{"http_proxy"} = 'http://taken.proxy.example:8080';
		local $ENV{"HTTP_PROXY"} = 'http://discarded.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.proxy.example:8080 */', "Correctly took 'http_proxy' environment variable over 'HTTP_PROXY': ".$result->output );
		delete(local $ENV{"http_proxy"});
		delete(local $ENV{"HTTP_PROXY"});

		# Test: Two environment variables, 'http_proxy' and 'HTTP_PROXY', alongside -x argument which should override both
		local $ENV{"http_proxy"} = 'http://discarded1.proxy.example:8080';
		local $ENV{"HTTP_PROXY"} = 'http://discarded2.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 -x 'http://taken.proxy.example:8080' -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.proxy.example:8080 */', "-X proxy argument was taken over 'http_proxy' and 'HTTP_PROXY' environment variables: ".$result->output );
		delete(local $ENV{"http_proxy"});
		delete(local $ENV{"HTTP_PROXY"});

		# Test: Two environment variables, 'http_proxy' and 'HTTP_PROXY', alongside --proxy argument which should override both
		local $ENV{"http_proxy"} = 'http://discarded1.proxy.example:8080';
		local $ENV{"HTTP_PROXY"} = 'http://discarded2.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 --proxy 'http://taken.example.com:8080' -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.example.com:8080 */', "--proxy argument was taken over 'http_proxy' and 'HTTP_PROXY' environment variables: ".$result->output );
		delete(local $ENV{"http_proxy"});
		delete(local $ENV{"HTTP_PROXY"});
	}
}

# and the same for SSL
SKIP: {
	skip "libcurl version is smaller than $required_version and/or HTTP::Daemon::SSL not installed", 6 if ! exists $servers->{https} or not $use_advanced_checks;
	# https without virtual port
	$cmd = "./$plugin -H $virtual_host -I 127.0.0.1 -p $port_https --ssl -u /virtual_port -r ^$virtual_host:$port_https\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# https with virtual port (!= 443)
	$cmd = "./$plugin -H $virtual_host:$virtual_port -I 127.0.0.1 -p $port_https --ssl -u /virtual_port -r ^$virtual_host:$virtual_port\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# https with virtual port (443)
	$cmd = "./$plugin -H $virtual_host:443 -I 127.0.0.1 -p $port_https --ssl -u /virtual_port -r ^$virtual_host\$";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# curlopt proxy/noproxy parsing tests, ssl enabled
	{
		# Make a scope and change environment variables here, to not mess them up for other tests using environment variables

		# Test: Only environment variable 'https_proxy', should be picked up
		local $ENV{"https_proxy"} = 'http://proxy.example.com:8080';
		$cmd = "$command -u /statuscode/200 --ssl -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://proxy.example.com:8080 */', "Correctly took 'https_proxy' environment variable: ".$result->output );
		delete($ENV{"https_proxy"});

		# Test: Two environment variables, 'https_proxy' and 'HTTPS_PROXY', lowercase should be used
		local $ENV{"https_proxy"} = 'http://taken.proxy.example:8080';
		local $ENV{"HTTPS_PROXY"} = 'http://discarded.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 --ssl -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.proxy.example:8080 */', "Correctly took 'https_proxy' environment variable over 'HTTPS_PROXY': ".$result->output );
		delete(local $ENV{"https_proxy"});
		delete(local $ENV{"HTTPS_PROXY"});

		# Test: Two environment variables, 'https_proxy' and 'HTTPS_PROXY', alongside -x argument which should override both
		local $ENV{"https_proxy"} = 'http://discarded1.proxy.example:8080';
		local $ENV{"HTTPS_PROXY"} = 'http://discarded2.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 --ssl -x 'http://taken.example.com:8080' -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.example.com:8080 */', "Argument -x overwrote environment variables 'https_proxy' and 'HTTPS_PROXY': ".$result->output );
		delete(local $ENV{"http_proxy"});
		delete(local $ENV{"HTTP_PROXY"});

		# Test: Two environment variables, 'http_proxy' and 'HTTP_PROXY', alongside --proxy argument which should override both
		local $ENV{"https_proxy"} = 'http://discarded1.proxy.example:8080';
		local $ENV{"HTTPS_PROXY"} = 'http://discarded2.proxy.example:8080';
		$cmd = "$command -u /statuscode/200 --ssl --proxy 'http://taken.example.com:8080' -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.*CURLOPT_PROXY: http://taken.example.com:8080 */', "Argument --proxy overwrote environment variables 'https_proxy' and 'HTTPS_PROXY': ".$result->output );
		delete(local $ENV{"https_proxy"});
		delete(local $ENV{"HTTPS_PROXY"});
	}



}


sub run_common_tests {
	my ($opts) = @_;
	my $command = $opts->{command};
	if ($opts->{ssl}) {
		$command .= " --ssl";
	}

	$result = NPTest->testCmd( "$command -u /file/root" );
	is( $result->return_code, 0, "/file/root");
	like( $result->output, '/.*HTTP/1.1 200 OK - 274 bytes in [\d\.]+ second.*/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /file/root -s Root" );
	is( $result->return_code, 0, "/file/root search for string");
	like( $result->output, '/.*HTTP/1.1 200 OK - 274 bytes in [\d\.]+ second.*/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /file/root -s NonRoot" );
	is( $result->return_code, 2, "Missing string check");
	like( $result->output, qr%string 'NonRoot' not found on 'https?://127\.0\.0\.1:\d+/file/root'%, "Shows search string and location");

	$result = NPTest->testCmd( "$command -u /file/root -s NonRootWithOver30charsAndMoreFunThanAWetFish" );
	is( $result->return_code, 2, "Missing string check");
	like( $result->output, qr%string 'NonRootWithOver30charsAndM...' not found on 'https?://127\.0\.0\.1:\d+/file/root'%, "Shows search string and location");

	$result = NPTest->testCmd( "$command -u /header_check -d foo" );
	is( $result->return_code, 0, "header_check search for string");
	like( $result->output, '/.*HTTP/1.1 200 OK - 96 bytes in [\d\.]+ second.*/', "Output correct" );

	$result = NPTest->testCmd( "$command -u /header_check -d bar" );
	is( $result->return_code, 2, "Missing header string check");
	like( $result->output, qr%header 'bar' not found on 'https?://127\.0\.0\.1:\d+/header_check'%, "Shows search string and location");

	$result = NPTest->testCmd( "$command -u /header_broken_check" );
	is( $result->return_code, 0, "header_check search for string");
	like( $result->output, '/.*HTTP/1.1 200 OK - 138 bytes in [\d\.]+ second.*/', "Output correct" );

	my $cmd;
	$cmd = "$command -u /slow";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, "$cmd");
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );
	$result->output =~ /in ([\d\.]+) second/;
	cmp_ok( $1, ">", 1, "Time is > 1 second" );

	$cmd = "$command -u /statuscode/200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/200 -e 200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*Status line output matched "200".*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 201 Created - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 201";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*Status line output matched "201".*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 200";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 2, $cmd);
	like( $result->output, '/.*Invalid HTTP response received from host on port \d+: HTTP/1.1 201 Created.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/200 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*Status line output matched "200,201,202".*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/201 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*Status line output matched "200,201,202".*/', "Output correct: ".$result->output );

	$cmd = "$command -u /statuscode/203 -e 200,201,202";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 2, $cmd);
	like( $result->output, '/.*Invalid HTTP response received from host on port (\d+): HTTP/1.1 203 Non-Authoritative Information.*/', "Output correct: ".$result->output );

	$cmd = "$command -j HEAD -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 HEAD - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -j POST -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 POST - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -j GET -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 GET - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 GET - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -P foo -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 POST - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -j DELETE -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 1, $cmd);
	like( $result->output, '/.*HTTP/1.1 405 Method Not Allowed.*/', "Output correct: ".$result->output );

	$cmd = "$command -j foo -u /method";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 2, $cmd);
	like( $result->output, '/.*HTTP/1.1 501 Not Implemented.*/', "Output correct: ".$result->output );

	$cmd = "$command -P stufftoinclude -u /postdata -s POST:stufftoinclude";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -j PUT -P stufftoinclude -u /postdata -s PUT:stufftoinclude";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# To confirm that the free doesn't segfault
	$cmd = "$command -P stufftoinclude -j PUT -u /postdata -s PUT:stufftoinclude";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /redirect";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 301 Moved Permanently - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -f follow -u /redirect";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 301 Moved Permanently - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -f follow -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -f sticky -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -f stickyport -u /redirect -k 'follow: me'";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	$cmd = "$command -f follow -u /redirect_rel -s redirected";
	$result = NPTest->testCmd( $cmd );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct: ".$result->output );

	# Redirect with increment tests. These are for checking if the url parameters, query parameters and fragment are parsed.
	# The server at this point has dynamic redirection. It tries to increment values that it sees in these fields, then redirects.
	# It also appends some debug log and writes it into HTTP content, pass the -vvv parameter to see them.

	$cmd = "$command -u '/redirect_with_increment/path1/path2/path3/path4' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 1, $cmd);
	like( $result->output, '/.*HTTP/1.1 403 Forbidden - \d+ bytes in [\d\.]+ second.*/', "Output correct, redirect_count was not present, got redirected to / : ".$result->output );

	# redirect_count=0 is parsed as a parameter and incremented. When it goes up to 3, the redirection returns HTTP OK
	$cmd = "$command -u '/redirect_with_increment/path1/path2;redirect_count=0;p1=1;p2=ab?qp1=10&qp2=kl#f1=test' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, redirect_count went up to 3, and returned OK: ".$result->output );

	# location_redirect_count=0 goes up to 3, which uses the HTTP 302 style of redirection with 'Location' header
	$cmd = "$command -u '/redirect_with_increment/path1/path2;location_redirect_count=0;p1=1;p2=ab?qp1=10&qp2=kl#f1=test' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, location_redirect_count went up to 3: ".$result->output );

	# fail_count parameter may also go up to 3, which returns a HTTP 403
	$cmd = "$command -u '/redirect_with_increment/path1/path2;redirect_count=0;fail_count=2' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 1, $cmd);
	like( $result->output, '/.*HTTP/1.1 403 Forbidden - \d+ bytes in [\d\.]+ second.*/', "Output correct, early due to fail_count reaching 3: ".$result->output );

	# redirect_count=0, p1=1 , p2=ab => redirect_count=1, p1=2 , p2=bc => redirect_count=2, p1=3 , p2=cd => redirect_count=3 , p1=4 , p2=de
	# Last visited URI returns HTTP OK instead of redirect, and the one before that contains the new_uri in its content
	$cmd = "$command -u '/redirect_with_increment/path1/path2;redirect_count=0;p1=1;p2=ab?qp1=10&qp2=kl#f1=test' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*redirect_count=3;p1=4;p2=de\?*/', "Output correct, parsed and incremented both parameters p1 and p2 : ".$result->output );
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, location_redirect_count went up to 3: ".$result->output );

	# Same incrementation as before, uses the query parameters that come after the first '?' : qp1 and qp2
	$cmd = "$command -u '/redirect_with_increment/path1/path2;redirect_count=0;p1=1;p2=ab?qp1=10&qp2=kl#f1=test' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*\?qp1=13&qp2=no*/', "Output correct, parsed and incremented both query parameters qp1 and qp2 : ".$result->output );
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, location_redirect_count went up to 3: ".$result->output );

	# Check if the query parameter order is kept intact
	$cmd = "$command -u '/redirect_with_increment;redirect_count=0;?qp0=0&qp1=1&qp2=2&qp3=3&qp4=4&qp5=5' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*\?qp0=3&qp1=4&qp2=5&qp3=6&qp4=7&qp5=8*/', "Output correct, parsed and incremented query parameters qp1,qp2,qp3,qp4,qp5 in order : ".$result->output );
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, location_redirect_count went up to 3: ".$result->output );

	# The fragment is passed as another parameter.
	# During the server redirects the fragment will be set to its value, if such a key is present.
	# 'ebiil' => 'fcjjm' => 'gdkkn' => 'hello'
	$cmd = "$command -u '/redirect_with_increment/path1/path2;redirect_count=0;fragment=ebiil?qp1=0' --onredirect=follow -vvv";
	$result = NPTest->testCmd( "$cmd" );
	is( $result->return_code, 0, $cmd);
	like( $result->output, '/.*redirect_count=3;fragment=hello\?qp1=3#hello*/', "Output correct, fragments are specified by server and followed by check_curl: ".$result->output );
	like( $result->output, '/.*HTTP/1.1 200 OK - \d+ bytes in [\d\.]+ second.*/', "Output correct, location_redirect_count went up to 3: ".$result->output );

	# These tests may block
	# stickyport - on full urlS port is set back to 80 otherwise
	$cmd = "$command -f stickyport -u /redir_external -t 5 -s redirected";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(2);
		$result = NPTest->testCmd( $cmd );
	};
	alarm(0);
	isnt( $@, "alarm\n", $cmd );
	is( $result->return_code, 0, $cmd );

	# Let's hope there won't be any web server on :80 returning "redirected"!
	$cmd = "$command -f sticky -u /redir_external -t 5 -s redirected";
	eval {
		local $SIG{ALRM} = sub { die "alarm\n" };
		alarm(2);
		$result = NPTest->testCmd( $cmd );
	};
	alarm(0);
	isnt( $@, "alarm\n", $cmd );
	isnt( $result->return_code, 0, $cmd );

	# Test an external address - timeout
	SKIP: {
		skip "This doesn't seem to work all the time", 1 unless ($ENV{HTTP_EXTERNAL});
		$cmd = "$command -f follow -u /redir_external -t 5";
		eval {
			$result = NPTest->testCmd( $cmd, 2 );
		};
		like( $@, "/timeout in command: $cmd/", $cmd );
	}

	$cmd = "$command -u /timeout -t 5";
	eval {
		$result = NPTest->testCmd( $cmd, 2 );
	};
	like( $@, "/timeout in command: $cmd/", $cmd );

	$cmd = "$command -f follow -u /redir_timeout -t 2";
	eval {
		$result = NPTest->testCmd( $cmd, 5 );
	};
	is( $@, "", $cmd );

	$cmd = "$command -u /chunked -s 'chunkedencodingtest' -d 'Transfer-Encoding: chunked'";
	eval {
		$result = NPTest->testCmd( $cmd, 5 );
	};
	is( $@, "", $cmd );

	# curlopt proxy/noproxy parsing tests
	# Make a scope and change environment variables here, to not mess them up for other tests using environment variables
	{
		# Noproxy tests

		# Test: Only environment variable 'no_proxy', should be picked up
		local $ENV{"no_proxy"} = 'internal.acme.org';
		$cmd = "$command -u /statuscode/200 -v";
		$result = NPTest->testCmd( $cmd );
		like( $result->output, '/.* curl CURLOPT_NOPROXY: internal.acme.org */', "Correctly took 'no_proxy' environment variable: ".$result->output );
		delete($ENV{"no_proxy"});

		# Test: Two environment variables, 'no_proxy' and 'NO_PROXY', lowercase should be used
		local $ENV{"no_proxy"} = 'taken.acme.org';
		local $ENV{"NO_PROXY"} = 'discarded.acme.org';
		$cmd = "$command -u /statuscode/200 -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*CURLOPT_NOPROXY: taken.acme.org*/', "Correctly took 'no_proxy' environment variable over 'NO_PROXY': ".$result->output );
		delete(local $ENV{"no_proxy"});
		delete(local $ENV{"NO_PROXY"});

		# Test: Two environment variables, 'no_proxy' and 'NO_PROXY', alongside --noproxy argument which should override both
		local $ENV{"no_proxy"} = 'taken.acme.org';
		local $ENV{"NO_PROXY"} = 'discarded.acme.org';
		$cmd = "$command -u /statuscode/200 --noproxy 'taken.acme.org' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*CURLOPT_NOPROXY: taken.acme.org*/', "Argument --noproxy overwrote environment variables 'no_proxy' and 'NO_PROXY': ".$result->output );
		delete(local $ENV{"no_proxy"});
		delete(local $ENV{"NO_PROXY"});

		# Test: Noproxy given as many domains, separated by commas
		$cmd = "$command -u /statuscode/200 --noproxy 'internal1.acme.org,internal2.acme.org,internal3.acme.org' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*CURLOPT_NOPROXY: internal1.acme.org,internal2.acme.org,internal3.acme.org*/', "Argument --noproxy read multiple noproxy domains: ".$result->output );

		# Test: Noproxy given as various IPv4 addresses / CIDR domains
		$cmd = "$command -u /statuscode/200 --noproxy '10.11.12.13,256.256.256.256,0.0.0.0,192.156.0.0/22,10.0.0.0/4' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*CURLOPT_NOPROXY: 10.11.12.13,256.256.256.256,0.0.0.0,192.156.0.0/22,10.0.0.0/4*/', "Argument --noproxy took multiple noproxy domains: ".$result->output );

		# Test: Noproxy given as various IPv6 addresses / CIDR domains
		$cmd = "$command -u /statuscode/200 --noproxy '0123:4567:89AB:CDEF:0123:4567:89AB:CDEF,0123::CDEF,0123:4567/96,[::1],::1,[1234::5678:ABCD/4]' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*CURLOPT_NOPROXY: 0123:4567:89AB:CDEF:0123:4567:89AB:CDEF,0123::CDEF,0123:4567\/96,\[::1\],::1,\[1234::5678:ABCD\/4\].*/', "Argument --noproxy took multiple noproxy domains: ".$result->output );

		# Test: Invalid IP addresses, check for nonzero return code
		$cmd = "$command -u /statuscode/200 --noproxy '300.400.500.600,1.2.3,XYZD:0123::,1:2:3:4:5:6:7,1::2::3,1.1.1.1/64,::/256' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);

		# Test: Test if noproxy argument picks up special '*' as noproxy
		$cmd = "$command -u /statuscode/200 --proxy http://proxy.example.com:8080 --noproxy '*' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*proxy_resolves_hostname: 0.*/', "Proxy will not be used due to '*' in noproxy: ".$result->output );

		# Test: Test if a direct match with the hostname
		$cmd = "$command -u /statuscode/200 --proxy http://proxy.example.com:8080 --noproxy '*' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*proxy_resolves_hostname: 0.*/', "Proxy will not be used due to '*' in noproxy: ".$result->output );

		# Test: Test if a direct match with the hostname
		$cmd = "$command -u /statuscode/200 --proxy http://proxy.example.com:8080 --noproxy '127.0.0.1' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*proxy_resolves_hostname: 0.*/', "Proxy will not be used due to '127.0.0.1' in noproxy: ".$result->output );

		# Test: Test if a direct match with the IP
		$cmd = "$command -u /statuscode/200 --proxy http://proxy.example.com:8080 --noproxy '127.0.0.1' -v";
		$result = NPTest->testCmd( $cmd );
		is( $result->return_code, 0, $cmd);
		like( $result->output, '/.*proxy_resolves_hostname: 0.*/', "Proxy will not be used due to '127.0.0.1' in noproxy: ".$result->output );

	}

}
