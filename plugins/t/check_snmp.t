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
    plan tests => 62;
}

my $res;

my $host_snmp          = getTestParameter("NP_HOST_SNMP", "A host providing an SNMP Service", "localhost");
my $snmp_community     = getTestParameter("NP_SNMP_COMMUNITY", "The SNMP Community string for SNMP Testing (assumes snmp v1)", "public");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");
my $user_snmp          = getTestParameter("NP_SNMP_USER", "An SNMP user", "auth_md5");


$res = NPTest->testCmd( "./check_snmp -t 1" );
is( $res->return_code, 3, "No host name" );
is( $res->output, "No OIDs specified" );

$res = NPTest->testCmd( "./check_snmp -H fakehostname --ignore-mib-parsing-errors" );
is( $res->return_code, 3, "No OIDs specified" );
is( $res->output, "No OIDs specified" );

$res = NPTest->testCmd( "./check_snmp -H fakehost --ignore-mib-parsing-errors -o oids -P 3 -U not_a_user --seclevel=rubbish" );
is( $res->return_code, 3, "Invalid seclevel" );
like( $res->output, "/invalid security level: rubbish/" );

$res = NPTest->testCmd( "./check_snmp -H fakehost --ignore-mib-parsing-errors -o oids -P 3c" );
is( $res->return_code, 3, "Invalid protocol" );
like( $res->output, "/invalid SNMP version/protocol: 3c/" );

SKIP: {
    skip "no snmp host defined", 50 if ( ! $host_snmp );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -P 2c -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying uptime" );
    $res->output =~ /\|.*=(\d+);/;
    my $value = $1;
    cmp_ok( $value, ">", 0, "Got a time value" );
    like($res->perf_output, "/sysUpTime.*$1/", "Got perfdata with value '$1' in it");


    # some more threshold tests
    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c 1 -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c 1" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c 1: -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Threshold test -c 1:" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c ~:1 -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c ~:1" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c 1:10 -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Threshold test -c 1:10" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c \@1:10 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Threshold test -c \@1:10" );


    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o .1.3.6.1.2.1.1.3.0 -w 1: -c 1: -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Test with numeric OID (no mibs loaded)" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysDescr.0 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying sysDescr" );
    unlike($res->perf_output, '/sysDescr/', "Perfdata doesn't contain string values");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysDescr.0,system.sysDescr.0 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying two string OIDs, comma-separated" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysDescr.0 -o system.sysDescr.0 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying two string OIDs, repeated option" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 1:1 -c 1:1 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying hrSWRunIndex.1" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 0   -c 1: -P 2c");
    cmp_ok( $res->return_code, '==', 1, "Exit WARNING when querying hrSWRunIndex.1 and warn-th doesn't apply " );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w  :0 -c 0 -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL when querying hrSWRunIndex.1 and crit-th doesn't apply" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o ifIndex.2,ifIndex.1 -w 1:2 -c 1:2 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Checking two OIDs at once" );
    like( $res->perf_output, "/ifIndex.2'?=2/", "Got 1st perf data" );
    like( $res->perf_output, "/ifIndex.1'?=1/", "Got 2nd perf data" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o ifIndex.2,ifIndex.1 -w 1:2,1:2 -c 2:2,2:2 -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Checking critical threshold is passed if any one value crosses" );
    like( $res->perf_output, "/ifIndex.2'?=2/", "Got 1st perf data" );
    like( $res->perf_output, "/ifIndex.1'?=1/", "Got 2nd perf data" );

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w 1:,1: -c 1:,1: -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK when querying hrMemorySize and hrSystemProcesses");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w \@:0 -c \@0 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Exit OK with inside-range thresholds");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o enterprises.ucdavis.laTable.laEntry.laLoadInt.3 -P 2c");
    $res->output =~ m/^.*Value: (\d+).*$/;
    my $lower = $1 - 0.05;
    my $higher = $1 + 0.05;
    # $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o enterprises.ucdavis.laTable.laEntry.laLoadInt.3 -w $lower -c $higher -P 2c");
    # cmp_ok( $res->return_code, '==', 1, "Exit WARNING with fractional arguments");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0,host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w ,:0 -c ,:2 -P 2c");
    cmp_ok( $res->return_code, '==', 1, "Exit WARNING on 2nd threshold");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w '' -c '' -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Empty thresholds doesn't crash");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w ,,1 -c ,,2 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Skipping first two thresholds on 2 OID check");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o host.hrStorage.hrMemorySize.0,host.hrSystem.hrSystemProcesses.0 -w ,, -c ,, -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Skipping all thresholds");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -c 1000000000000: -u '1/100 sec' -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Timetick used as a threshold");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o system.sysUpTime.0 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "Timetick used as a string");

    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -C $snmp_community -o HOST-RESOURCES-MIB::hrSWRunName.1 -P 2c");
    cmp_ok( $res->return_code, '==', 0, "snmp response without datatype");
}

SKIP: {
    skip "no SNMP user defined", 1 if ( ! $user_snmp );
    $res = NPTest->testCmd( "./check_snmp -H $host_snmp --ignore-mib-parsing-errors -o HOST-RESOURCES-MIB::hrSystemUptime.0 -P 3 -U $user_snmp -L noAuthNoPriv");
}

# These checks need a complete command line. An invalid community is used so
# the tests can run on hosts w/o snmp host/community in NPTest.cache. Execution will fail anyway
SKIP: {
    skip "no non responsive host defined", 2 if ( ! $host_nonresponsive );
    $res = NPTest->testCmd( "./check_snmp -H $host_nonresponsive --ignore-mib-parsing-errors -C np_foobar -o system.sysUpTime.0 -w 1: -c 1: -P 2c");
    cmp_ok( $res->return_code, '==', 2, "Exit CRITICAL with non responsive host" );
    # like($res->output, '/Plugin timed out while executing system call/', "String matches timeout problem");
}

SKIP: {
    skip "no non invalid host defined", 2 if ( ! $hostname_invalid );
    $res = NPTest->testCmd( "./check_snmp -H $hostname_invalid --ignore-mib-parsing-errors -C np_foobar -o system.sysUpTime.0 -w 1: -c 1: -P 2c");
    cmp_ok( $res->return_code, '==', 3, "Exit UNKNOWN with non responsive host" );
    like($res->output, '/.*Unknown host.*/s', "String matches invalid host");
}
