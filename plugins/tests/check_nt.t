#! /usr/bin/perl -w -I ..
#
# Test check_nt by having a stub check_nt daemon
#

use strict;
use Test::More;
use NPTest;
use FindBin qw($Bin);

use IO::Socket;
use IO::Select;
use POSIX;

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

	my $server = IO::Socket::INET->new(
		LocalPort => $port,
		Type => SOCK_STREAM,
		Reuse => 1,
		Proto => "tcp",
		Listen => 10,
	) or die "Cannot be a tcp server on port $port: $@";

	$server->autoflush(1);

	print "Please contact me at port $port\n";
	while (my $client = $server->accept ) {
		my $data = "";
		my $rv = $client->recv($data, POSIX::BUFSIZ, 0);

		my ($password, $command, $arg) = split('&', $data);
		
		if ($command eq "4") {
			if ($arg eq "c") {
				print $client "930000000&1000000000";
			} elsif ($arg eq "d") {
				print $client "UNKNOWN: Drive is not a fixed drive";
			}
		}
	}
	exit;
}

END { if ($pid) { print "Killing $pid\n"; kill "INT", $pid } };

if ($ARGV[0] && $ARGV[0] eq "-d") {
	sleep 1000;
}

if (-x "./check_nt") {
	plan tests => 5;
} else {
	plan skip_all => "No check_nt compiled";
}

my $result;
my $command = "./check_nt -H 127.0.0.1 -p $port";

$result = NPTest->testCmd( "$command -v USEDDISKSPACE -l c" );
is( $result->return_code, 0, "USEDDISKSPACE c");
is( $result->output, q{c:\ - total: 0.93 Gb - used: 0.07 Gb (7%) - free 0.87 Gb (93%) | 'c:\ Used Space'=0.07Gb;0.00;0.00;0.00;0.93}, "Output right" );

$result = NPTest->testCmd( "$command -v USEDDISKSPACE -l d" );
is( $result->return_code, 3, "USEDDISKSPACE d - invalid");
is( $result->output, "Free disk space : Invalid drive", "Output right" );

$result = NPTest->testCmd( "./check_nt -v USEDDISKSPACE -l d" );
is( $result->return_code, 3, "Fail if -H missing");

