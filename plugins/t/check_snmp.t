#! /usr/bin/perl -w

use strict;
use Helper;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 8; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;
my $community=get_option("snmp_community","SNMP community name");

exit(0) unless (-x "./check_snmp");

$cmd = "./check_snmp -H 127.0.0.1 -C $community -o system.sysUpTime.0 -w 1: -c 1:";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
chomp $str;
$t += ok $str, '/^SNMP OK - \d+/';

$cmd = "./check_snmp -H 127.0.0.1 -C $community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 1:1 -c 1:1";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
chomp $str;
$t += ok $str, '/^SNMP OK - 1\s*$/';

$cmd = "./check_snmp -H 127.0.0.1 -C $community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 0 -c 1:";
$str = `$cmd`;
$t += ok $?>>8,1;
print "Test was: $cmd\n" unless ($?);
chomp $str;
$t += ok $str, '/^SNMP WARNING - \*1\*\s*$/';

$cmd = "./check_snmp -H 127.0.0.1 -C $community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w :0 -c 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
chomp $str;
$t += ok $str, '/^SNMP CRITICAL - \*1\*\s*$/';

#host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 = 1
#enterprises.ucdavis.memory.memAvailSwap.0
#./check_snmp 127.0.0.1 -C staff -o enterprises.ucdavis.diskTable.dskEntry.dskAvail.1,enterprises.ucdavis.diskTable.dskEntry.dskPercent.1 -w 100000: -c 50000: -l Space on root -u 'bytes free (','% used)'

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
