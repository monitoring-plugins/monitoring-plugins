#! /usr/bin/perl -w -I ..
#
# Simple Network Management Protocol (SNMP) Test via check_snmp
#
#

use strict;
use Test::More;
use NPTest;

BEGIN {
    plan skip_all => 'check_snmp is not compiled' unless -x "./check_snmp";
    plan tests => 61;
}

my $res;

my $host_snmp = getTestParameter( "host_snmp",          "NP_HOST_SNMP",      "localhost",
                                  "A host providing an SNMP Service");

my $snmp_community = getTestParameter( "snmp_community",     "NP_SNMP_COMMUNITY", "public",
                                       "The SNMP Community string for SNMP Testing (assumes snmp v1)" );

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
                                           "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );
my $user_snmp = getTestParameter( "user_snmp",    "NP_SNMP_USER",    "auth_md5", "An SNMP user");

$res = NPTest->testCmd( "./check_snmp -t 1" );
is( $res->return_code, 3, "No host name" );
is( $res->output, "No host specified" );

$res = NPTest->testCmd( "./check_snmp -H fakehostname" );
is( $res->return_code, 3, "No OIDs specified" );
is( $res->output, "No OIDs specified" );

$res = NPTest->testCmd( "./check_snmp -H fakehost -o oids -P 3 -U not_a_user --seclevel=rubbish" );
is( $res->return_code, 3, "Invalid seclevel" );
like( $res->output, "/check_snmp: Invalid seclevel - rubbish/" );

$res = NPTest->testCmd( "./check_snmp -H fakehost -o oids -P 3c" );
is( $res->return_code, 3, "Invalid protocol" );
like( $res->output, "/check_snmp: Invalid SNMP version - 3c/" );

SKIP: {
    skip "no snmp host defined", 38 if ( ! $host_snmp );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying uptime" );
    like($res->output, '/^SNMP OK - (\d+)/', "String contains SNMP OK");
    $res->output =~ /^SNMP OK - (\d+)/;
    my $value = $1;
    cmp_ok( $value, ">", 0, "Got a time value" );
    like($res->perf_output, "/sysUpTime.*$1/", "Got perfdata with value '$1' in it");


    # some more threshold tests
    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c 1");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c 1" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c 1:");
    cmp_ok( $res->return_code, '==', 0, "Threshold test -c 1:" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c ~:1");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c ~:1" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c 1:10");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c 1:10" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c \@1:10");
    cmp_ok( $res->return_code, '==', 0, "Threshold test -c \@1:10" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c 10:1");
    cmp_ok( $res->return_code, '==', 0, "Threshold test -c 10:1" );


    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o .1.3.6.1.2.1.1.3.0 -w 1: -c 1:");
    cmp_ok( $res->return_code, '==', 0, "Test with numeric OID (no mibs loaded)" );
    like($res->output, '/^SNMP OK - \d+/', "String contains SNMP OK");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysDescr.0");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying sysDescr" );
    unlike($res->perf_output, '/sysDescr/', "Perfdata doesn't contain string values");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysDescr.0,system.sysDescr.0");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying two string OIDs, comma-separated" );
    like($res->output, '/^SNMP OK - /', "String contains SNMP OK");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysDescr.0 -o system.sysDescr.0");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying two string OIDs, repeated option" );
    like($res->output, '/^SNMP OK - /', "String contains SNMP OK");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 1:1 -c 1:1");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying hrSWRunIndex.1" );
    like($res->output, '/^SNMP OK - 1\s.*$/', "String fits SNMP OK and output format");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 0   -c 1:");
    cmp_ok( $res->return_code, '==', 1, "Exit WARNING when querying hrSWRunIndex.1 and warn-th doesn't apply " );
    like($res->output, '/^SNMP WARNING - \*1\*\s.*$/', "String matches SNMP WARNING and output format");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w  :0 -c 0");
    cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL when querying hrSWRunIndex.1 and crit-th doesn't apply" );
    like($res->output, '/^SNMP CRITICAL - \*1\*\s.*$/', "String matches SNMP CRITICAL and output format");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o ifIndex.2,ifIndex.1 -w 1:2 -c 1:2");
    cmp_ok( $res->return_code, '==', 0, "Checking two OIDs at once" );
    like($res->output, "/^SNMP OK - 2 1/", "Got two values back" );
    like( $res->perf_output, "/ifIndex.2=2/", "Got 1st perf data" );
    like( $res->perf_output, "/ifIndex.1=1/", "Got 2nd perf data" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o ifIndex.2,ifIndex.1 -w 1:2,1:2 -c 2:2,2:2");
    cmp_ok( $res->return_code, '==', 2, "Checking critical threshold is passed if any one value crosses" );
    like($res->output, "/^SNMP CRITICAL - 2 *1*/", "Got two values back" );
    like( $res->perf_output, "/ifIndex.2=2/", "Got 1st perf data" );
    like( $res->perf_output, "/ifIndex.1=1/", "Got 2nd perf data" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w 1:,1: -c 1:,1:");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying hrMemorySize and hrSystemProcesses");
    like($res->output, '/^SNMP OK - \d+ \d+/', "String contains hrMemorySize and hrSystemProcesses");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w \@:0 -c \@0");
    cmp_ok( $res->return_code, '==', 0, "Exit OK with inside-range thresholds");
    like($res->output, '/^SNMP OK - 1\s.*$/', "String matches SNMP OK and output format");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o enterprises.ucdavis.laTable.laEntry.laLoad.3");
    $res->output =~ m/^SNMP OK - (\d+\.\d{2})\s.*$/;
    my $lower = $1 - 0.05;
    my $higher = $1 + 0.05;
    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o enterprises.ucdavis.laTable.laEntry.laLoad.3 -w $lower -c $higher");
    cmp_ok( $res->return_code, '==', 1, "Exit WARNING with fractionnal arguments");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0,host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w ,:0 -c ,:2");
    cmp_ok( $res->return_code, '==', 1, "Exit WARNING on 2nd threshold");
    like($res->output, '/^SNMP WARNING - Timeticks:\s\(\d+\)\s+(?:\d+ days?,\s+)?\d+:\d+:\d+\.\d+\s+\*1\*\s.*$/', "First OID returned as string, 2nd checked for thresholds");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w '' -c ''");
    cmp_ok( $res->return_code, '==', 0, "Empty thresholds doesn't crash");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w ,,1 -c ,,2");
    cmp_ok( $res->return_code, '==', 0, "Skipping first two thresholds on 2 OID check");
    like($res->output, '/^SNMP OK - \d+ \w+ \d+\s.*$/', "Skipping first two thresholds, result printed rather than parsed");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w ,, -c ,,");
    cmp_ok( $res->return_code, '==', 0, "Skipping all thresholds");
    like($res->output, '/^SNMP OK - \d+ \w+ \d+\s.*$/', "Skipping all thresholds, result printed rather than parsed");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -c 1000000000000: -u '1/100 sec'");
    cmp_ok( $res->return_code, '==', 2, "Timetick used as a threshold");
    like($res->output, '/^SNMP CRITICAL - \*\d+\* 1\/100 sec.*$/', "Timetick used as a threshold, parsed as numeric");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0");
    cmp_ok( $res->return_code, '==', 0, "Timetick used as a string");
    like($res->output, '/^SNMP OK - Timeticks:\s\(\d+\)\s+(?:\d+ days?,\s+)?\d+:\d+:\d+\.\d+\s.*$/', "Timetick used as a string, result printed rather than parsed");
}

SKIP: {
    skip "no SNMP user defined", 1 if ( ! $user_snmp );
    $res = NPTest->testCmd( "./check_snmp -H $host_snmp -o HOST-RESOURCES-MIB::hrSystemUptime.0 -P 3 -U $user_snmp -L noAuthNoPriv");
    like( $res->output, '/^SNMP OK - Timeticks:\s\(\d+\)\s+(?:\d+ days?,\s+)?\d+:\d+:\d+\.\d+\s.*$/', "noAuthNoPriv security level works properly" );
}

# These checks need a complete command line. An invalid community is used so
# the tests can run on hosts w/o snmp host/community in NPTest.cache. Execution will fail anyway
SKIP: {
    skip "no non responsive host defined", 2 if ( ! $host_nonresponsive );
    $res = NPTest->testCmd( "./check_snmp -H $host_nonresponsive -C np_foobar -o system.sysUpTime.0 -w 1: -c 1:");
    cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with non responsive host" );
    like($res->output, '/External command error: Timeout: No Response from /', "String matches timeout problem");
}

SKIP: {
    skip "no non invalid host defined", 2 if ( ! $hostname_invalid );
    $res = NPTest->testCmd( "./check_snmp -H $hostname_invalid   -C np_foobar -o system.sysUpTime.0 -w 1: -c 1:");
    cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with non responsive host" );
    like($res->output, '/External command error: .*(nosuchhost|Name or service not known|Unknown host)/', "String matches invalid host");
}
