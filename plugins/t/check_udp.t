#! /usr/bin/perl -w

#use strict;
use Cache;
use Helper;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

my $null = '';
my $str;
my $t;
my $hostname=get_option("udp_hostname","UDP host name");

$str = `./check_udp $hostname -p 37 -wt 300 -ct 600`;
$t += ok $?>>8,0;
$t += ok $str, '/^Connection accepted on port 37 - [0-9]+ second response time$/';

$str = `./check_udp $Cache::nullhost -p 80 -wt 0 -ct 0 -to 1`;
$t += ok $?>>8,2;

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
