#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

my $null = '';
my $str;
my $t;

$str = `./check_http $Cache::hostname -wt 300 -ct 600`;
$t += ok $?>>8,0;
$t += ok $str, '/HTTP\/1.1 [0-9]{3} (OK|Found) - [0-9]+ second response time/';

$str = `./check_http $Cache::nullhost -wt 1 -ct 2`;
$t += ok $?>>8,2;

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
