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

$cmd = "./check_pop $Cache::mailhost";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);

$cmd = "./check_pop -H $Cache::mailhost -p 110 -w 9 -c 9 -t 10 -e '+OK'";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);

$cmd = "./check_pop $Cache::mailhost -p 110 -wt 9 -ct 9 -to 10 -e '+OK'";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
