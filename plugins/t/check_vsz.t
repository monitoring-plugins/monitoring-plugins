#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 4; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_vsz 100000 1000000 init";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^ok \(all VSZ\<[0-9]+\)/';

$cmd = "./check_vsz 0 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL \(VSZ\>[0-9]+\)/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
