#! /usr/bin/perl -w

use strict;
use Helper;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 4; plan tests => $tests}

exit(0) unless (-x "./check_hpjd");

my $null = '';
my $cmd;
my $str;
my $t;
my $printer = get_option("hpjd_printer","HP Jet-Direct card address");

$cmd = "./check_hpjd $printer";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^Printer ok - /';

$cmd = "./check_hpjd $Cache::noserver";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/Timeout: No response from /';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
