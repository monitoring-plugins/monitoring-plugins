#! /usr/bin/perl -w

use strict;
use Test;
use vars qw($tests);

BEGIN {$tests = 4; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_load 100 100 100 100 100 100";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^load average: +[\.0-9]+, +[\.0-9]+, +[\.0-9]+$/';

$cmd = "./check_load 0 0 0 0 0 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^load average: +[\.0-9]+, +[\.0-9]+, +[\.0-9]+ CRITICAL$/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
