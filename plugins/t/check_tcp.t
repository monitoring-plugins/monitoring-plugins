#! /usr/bin/perl -w

#use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_tcp $Cache::hostname -p 80 -wt 300 -ct 600";
$str = `$cmd`;
$t += ok $?>>8,0;
print "$cmd\n" if ($?);
$t += ok $str, '/^TCP OK\s-\s+[0-9]?\.?[0-9]+ second response time on port 80/';

$cmd = "./check_tcp $Cache::nullhost -p 81 -wt 0 -ct 0 -to 1";
$str = `$cmd`;
$t += ok $?>>8,2;
print "$cmd\n" unless ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
