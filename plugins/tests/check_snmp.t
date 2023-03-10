#! /usr/bin/perl -w -I ..
#
# Test check_snmp by having an actual SNMP agent running
#

use strict;
use Test::More;
use NPTest;
use FindBin qw($Bin);
use POSIX qw/strftime/;

my $tests = 81;
# Check that all dependent modules are available
eval {
	require NetSNMP::OID;
	require NetSNMP::agent;
	require NetSNMP::ASN;
};

if ($@) {
	plan skip_all => "Missing required module for test: $@";
} else {
	if (-x "./check_snmp") {
        # check if snmpd has perl support
        my $test = `snmpd -c tests/conf/snmpd.conf -C -r -H 2>&1`;
        if(!defined $test) {
	        plan skip_all => "snmpd required";
        }
        elsif($test =~ m/Warning: Unknown token: perl/) {
	        plan skip_all => "snmpd has no perl support";
        } else {
		    plan tests => $tests;
        }
	} else {
		plan skip_all => "No check_snmp compiled";
	}
}

my $port_snmp = 16100 + int(rand(100));

my $faketime = -x '/usr/bin/faketime' ? 1 : 0;

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

# We should merge that with $ENV{'NPTEST_CACHE'}, use one dir for all test data
$ENV{'MP_STATE_PATH'} ||= "/var/tmp";

my $res;

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.0");
cmp_ok( $res->return_code, '==', 0, "Exit OK when querying a multi-line string" );
like($res->output, '/^SNMP OK - /', "String contains SNMP OK");
like($res->output, '/'.quotemeta('SNMP OK - Cisco Internetwork Operating System Software | 
.1.3.6.1.4.1.8072.3.2.67.0:
"Cisco Internetwork Operating System Software
IOS (tm) Catalyst 4000 \"L3\" Switch Software (cat4000-I9K91S-M), Version
12.2(20)EWA, RELEASE SOFTWARE (fc1)
Technical Support: http://www.cisco.com/techsupport
Copyright (c) 1986-2004 by cisco Systems, Inc.
"').'/m', "String contains all lines");

# sysContact.0 is "Alice" (from our snmpd.conf)
$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.0 -o sysContact.0 -o .1.3.6.1.4.1.8072.3.2.67.1");
cmp_ok( $res->return_code, '==', 0, "Exit OK when querying multi-line OIDs" );
like($res->output, '/^SNMP OK - /', "String contains SNMP OK");
like($res->output, '/'.quotemeta('SNMP OK - Cisco Internetwork Operating System Software ').'"?Alice"?'.quotemeta(' Kisco Outernetwork Oserating Gystem Totware | 
.1.3.6.1.4.1.8072.3.2.67.0:
"Cisco Internetwork Operating System Software
IOS (tm) Catalyst 4000 \"L3\" Switch Software (cat4000-I9K91S-M), Version
12.2(20)EWA, RELEASE SOFTWARE (fc1)
Technical Support: http://www.cisco.com/techsupport
Copyright (c) 1986-2004 by cisco Systems, Inc.
"
.1.3.6.1.4.1.8072.3.2.67.1:
"Kisco Outernetwork Oserating Gystem Totware
Copyleft (c) 2400-2689 by kisco Systrems, Inc."').'/m', "String contains all lines with multiple OIDs");

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.2");
like($res->output, '/'.quotemeta('SNMP OK - This should not confuse check_snmp \"parser\" | 
.1.3.6.1.4.1.8072.3.2.67.2:
"This should not confuse check_snmp \"parser\"
into thinking there is no 2nd line"').'/m', "Attempt to confuse parser No.1");

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.3");
like($res->output, '/'.quotemeta('SNMP OK - It\'s getting even harder if the line | 
.1.3.6.1.4.1.8072.3.2.67.3:
"It\'s getting even harder if the line
ends with with this: C:\\\\"').'/m', "Attempt to confuse parser No.2");

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.4");
like($res->output, '/'.quotemeta('SNMP OK - And now have fun with with this: \"C:\\\\\" | 
.1.3.6.1.4.1.8072.3.2.67.4:
"And now have fun with with this: \"C:\\\\\"
because we\'re not done yet!"').'/m', "Attempt to confuse parser No.3");

system("rm -f ".$ENV{'MP_STATE_PATH'}."/*/check_snmp/*");

# run rate checks with faketime. rate checks depend on the exact amount of time spend between the
# plugin runs which may fail on busy machines.
# using faketime removes this race condition and also saves all the sleeps in between.
SKIP: {
    skip "No faketime binary found", 28 if !$faketime;

    my $ts = time();
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -w 600" );
    is($res->return_code, 0, "Returns OK");
    is($res->output, "No previous data to calculate rate - assume okay");

    # test rate 1 second later
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -w 600" );
    is($res->return_code, 1, "WARNING - due to going above rate calculation" );
    is($res->output, "SNMP RATE WARNING - *666* | iso.3.6.1.4.1.8072.3.2.67.10=666;600 ");

    # test rate with same time
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -w 600" );
    is($res->return_code, 3, "UNKNOWN - basically the divide by zero error" );
    is($res->output, "Time duration between plugin calls is invalid");


    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -l inoctets" );
    is($res->return_code, 0, "OK for first call" );
    is($res->output, "No previous data to calculate rate - assume okay" );

    # test rate 1 second later
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -l inoctets" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP RATE OK - inoctets 666 | inoctets=666 ", "Check label");

    # test rate 3 seconds later
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+3))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -l inoctets" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP RATE OK - inoctets 333 | inoctets=333 ", "Check rate decreases due to longer interval");


    # label performance data check
    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 -l test" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - test 67996 | test=67996c ", "Check label");

    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 -l \"test'test\"" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - test'test 68662 | \"test'test\"=68662c ", "Check label");

    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 -l 'test\"test'" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - test\"test 69328 | 'test\"test'=69328c ", "Check label");

    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 -l test -O" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - test 69994 | iso.3.6.1.4.1.8072.3.2.67.10=69994c ", "Check label");

    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - 70660 | iso.3.6.1.4.1.8072.3.2.67.10=70660c ", "Check label");

    $res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 -l 'test test'" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP OK - test test 71326 | 'test test'=71326c ", "Check label");


    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -l inoctets_per_minute --rate-multiplier=60" );
    is($res->return_code, 0, "OK for first call" );
    is($res->output, "No previous data to calculate rate - assume okay" );

    # test 1 second later
    $res = NPTest->testCmd("LC_TIME=C TZ=UTC faketime -f '".strftime("%Y-%m-%d %H:%M:%S", localtime($ts+1))."' ./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10 --rate -l inoctets_per_minute --rate-multiplier=60" );
    is($res->return_code, 0, "OK as no thresholds" );
    is($res->output, "SNMP RATE OK - inoctets_per_minute 39960 | inoctets_per_minute=39960 ", "Checking multiplier");
};



$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.11 -s '\"stringtests\"'" );
is($res->return_code, 0, "OK as string matches" );
is($res->output, 'SNMP OK - "stringtests" | ', "Good string match" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.11 -s ring" );
is($res->return_code, 2, "CRITICAL as string doesn't match (though is a substring)" );
is($res->output, 'SNMP CRITICAL - *"stringtests"* | ', "Failed string match" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.11 --invert-search -s '\"stringtests\"'" );
is($res->return_code, 2, "CRITICAL as string matches but inverted" );
is($res->output, 'SNMP CRITICAL - *"stringtests"* | ', "Inverted string match" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.11 --invert-search -s ring" );
is($res->return_code, 0, "OK as string doesn't match but inverted" );
is($res->output, 'SNMP OK - "stringtests" | ', "OK as inverted string no match" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.12 -w 4:5" );
is($res->return_code, 1, "Numeric in string test" );
is($res->output, 'SNMP WARNING - *3.5* | iso.3.6.1.4.1.8072.3.2.67.12=3.5;4:5 ', "WARNING threshold checks for string masquerading as number" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.13" );
is($res->return_code, 0, "Not really numeric test" );
is($res->output, 'SNMP OK - "87.4startswithnumberbutshouldbestring" | ', "Check string with numeric start is still string" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.14" );
is($res->return_code, 0, "Not really numeric test (trying best to fool it)" );
is($res->output, 'SNMP OK - "555\"I said\"" | ', "Check string with a double quote following is still a string (looks like the perl routine will always escape though)" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.15 -r 'CUSTOM CHECK OK'" );
is($res->return_code, 0, "String check should check whole string, not a parsed number" );
is($res->output, 'SNMP OK - "CUSTOM CHECK OK: foo is 12345" | ', "String check witn numbers returns whole string");

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.16 -w -2: -c -3:" );
is($res->return_code, 0, "Negative integer check OK" );
is($res->output, 'SNMP OK - -2 | iso.3.6.1.4.1.8072.3.2.67.16=-2;-2:;-3: ', "Negative integer check OK output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.16 -w -2: -c -3:" );
is($res->return_code, 1, "Negative integer check WARNING" );
is($res->output, 'SNMP WARNING - *-3* | iso.3.6.1.4.1.8072.3.2.67.16=-3;-2:;-3: ', "Negative integer check WARNING output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.16 -w -2: -c -3:" );
is($res->return_code, 2, "Negative integer check CRITICAL" );
is($res->output, 'SNMP CRITICAL - *-4* | iso.3.6.1.4.1.8072.3.2.67.16=-4;-2:;-3: ', "Negative integer check CRITICAL output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.17 -w -3: -c -6:" );
is($res->return_code, 1, "Negative integer as string, WARNING" );
is($res->output, 'SNMP WARNING - *-4* | iso.3.6.1.4.1.8072.3.2.67.17=-4;-3:;-6: ', "Negative integer as string, WARNING output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.17 -w -2: -c -3:" );
is($res->return_code, 2, "Negative integer as string, CRITICAL" );
is($res->output, 'SNMP CRITICAL - *-4* | iso.3.6.1.4.1.8072.3.2.67.17=-4;-2:;-3: ', "Negative integer as string, CRITICAL output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.18 -c '~:-6.5'" );
is($res->return_code, 0, "Negative float OK" );
is($res->output, 'SNMP OK - -6.6 | iso.3.6.1.4.1.8072.3.2.67.18=-6.6;;@-6.5:~ ', "Negative float OK output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.18 -w '~:-6.65' -c '~:-6.55'" );
is($res->return_code, 1, "Negative float WARNING" );
is($res->output, 'SNMP WARNING - *-6.6* | iso.3.6.1.4.1.8072.3.2.67.18=-6.6;@-6.65:~;@-6.55:~ ', "Negative float WARNING output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10,.1.3.6.1.4.1.8072.3.2.67.17 -w '1:100000,-10:20' -c '2:200000,-20:30'" );
is($res->return_code, 0, "Multiple OIDs with thresholds" );
like($res->output, '/SNMP OK - \d+ -4 | iso.3.6.1.4.1.8072.3.2.67.10=\d+c;1:100000;2:200000 iso.3.6.1.4.1.8072.3.2.67.17=-4;-10:20;-20:30/', "Multiple OIDs with thresholds output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10,.1.3.6.1.4.1.8072.3.2.67.17 -w '1:100000,-1:2' -c '2:200000,-20:30'" );
is($res->return_code, 1, "Multiple OIDs with thresholds" );
like($res->output, '/SNMP WARNING - \d+ \*-4\* | iso.3.6.1.4.1.8072.3.2.67.10=\d+c;1:100000;2:200000 iso.3.6.1.4.1.8072.3.2.67.17=-4;-10:20;-20:30/', "Multiple OIDs with thresholds output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.10,.1.3.6.1.4.1.8072.3.2.67.17 -w 1,2 -c 1" );
is($res->return_code, 2, "Multiple OIDs with some thresholds" );
like($res->output, '/SNMP CRITICAL - \*\d+\* \*-4\* | iso.3.6.1.4.1.8072.3.2.67.10=\d+c;1;2 iso.3.6.1.4.1.8072.3.2.67.17=-4;;/', "Multiple OIDs with thresholds output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.19");
is($res->return_code, 0, "Test plain .1.3.6.1.4.1.8072.3.2.67.6 RC" );
is($res->output,'SNMP OK - 42 | iso.3.6.1.4.1.8072.3.2.67.19=42 ', "Test plain value of .1.3.6.1.4.1.8072.3.2.67.1" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.19 -M .1");
is($res->return_code, 0, "Test multiply RC" );
is($res->output,'SNMP OK - 4.200000 | iso.3.6.1.4.1.8072.3.2.67.19=4.200000 ' , "Test multiply .1 output" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.19 --multiplier=.1 -f '%.2f' ");
is($res->return_code, 0, "Test multiply RC + format" );
is($res->output, 'SNMP OK - 4.20 | iso.3.6.1.4.1.8072.3.2.67.19=4.20 ', "Test multiply .1 output + format" );

$res = NPTest->testCmd( "./check_snmp -H 127.0.0.1 -C public -p $port_snmp -o .1.3.6.1.4.1.8072.3.2.67.19 --multiplier=.1 -f '%.2f' -w 1");
is($res->return_code, 1, "Test multiply RC + format + thresholds" );
is($res->output, 'SNMP WARNING - *4.20* | iso.3.6.1.4.1.8072.3.2.67.19=4.20;1 ', "Test multiply .1 output + format + thresholds" );
