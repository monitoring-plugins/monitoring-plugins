#! /usr/bin/perl -w -I ..
#
# Test check_snmp by having an actual SNMP agent running
#

use strict;
use Test::More;
use NPTest;
use FindBin qw($Bin);

my $port_snmp = 16100 + int(rand(100));
my $running = 1;


# Start up server
my @pids;
my $pid = fork();
if ($pid) {
	# Parent
	push @pids, $pid;
	# give our agent some time to startup
	sleep(1);
} else {
	# Child
	#print "child\n";

	print "Please contact SNMP at: $port_snmp\n";
	close(STDERR); # Coment out to debug snmpd problems (most errors sent there are OK)
	exec("snmpd -c tests/conf/snmpd.conf -C -f -r udp:$port_snmp");
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

my $tests = 2;
if (-x "./check_snmp") {
	plan tests => $tests;
} else {
	plan skip_all => "No check_snmp compiled";
}

my $res;

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.0");
cmp_ok( $res->return_code, '==', 0, "Exit OK when querying a multi-line string" );
like($res->output, '/^SNMP OK - /', "String contains SNMP OK");

