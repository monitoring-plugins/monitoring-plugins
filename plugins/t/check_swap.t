#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 6; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_swap 100 100";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^Swap ok - Swap used\: +[0-9]{1,2}\% \([0-9]+ bytes out of [0-9]+\)$/';

$cmd = "./check_swap 0 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL - Swap used\: +[0-9]{1,2}\% \([0-9]+ bytes out of [0-9]+\)$/';

$cmd = "./check_swap 100 100 1000000000 1000000000";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL - Swap used\: +[0-9]{1,2}\% \([0-9]+ bytes out of [0-9]+\)$/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
