#! /usr/bin/perl -w

use strict;
use Cache;
use Helper;
use Test;
use vars qw($tests);

BEGIN {$tests = 6; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;
my $udp_hostname=get_option("udp_hostname","UDP host name");

# standard mode

$cmd = "./check_time -H $udp_hostname -w 999999,59 -c 999999,59 -t 60";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^TIME OK - [0-9]+ second time difference$/';

$cmd = "./check_time -H $udp_hostname -w 999999 -W 59 -c 999999 -C 59 -t 60";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^TIME OK - [0-9]+ second time difference$/';

# reverse compatibility mode

$cmd = "./check_time $udp_hostname -wt 59 -ct 59 -cd 999999 -wd 999999 -to 60";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^TIME OK - [0-9]+ second time difference$/';

# failure mode

#$cmd = "./check_time -H $Cache::nullhost -t 1";
#$str = `$cmd`;
#$t += ok $?>>8,255;
#print "Test was: $cmd\n" unless ($?);

#$cmd = "./check_time -H $Cache::noserver -t 1";
#$str = `$cmd`;
#$t += ok $?>>8,255;
#print "$cmd\n" unless ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
