#! /usr/bin/perl -w

use strict;
use Test;
use vars qw($tests);

BEGIN {$tests = 4; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_load -w 100,100,100 -c 100,100,100";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^OK - load average: [0-9]\.?[0-9]+, [0-9]\.?[0-9]+, [0-9]\.?[0-9]+/';

$cmd = "./check_load -w 0,0,0 -c 0,0,0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL - load average: [0-9]\.?[0-9]+, [0-9]\.?[0-9]+, [0-9]\.?[0-9]+/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
