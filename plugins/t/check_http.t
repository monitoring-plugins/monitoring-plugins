#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

my $null = '';
my $str;
my $t;

$str = `./check_http $Cache::httphost -wt 300 -ct 600`;
$t += ok $?>>8,0;
$t += ok $str, '/(HTTP\s[o|O][k|K]\s)?\s?HTTP\/1.[01]\s[0-9]{3}\s(OK|Found)\s-\s+[0-9]+\sbytes\sin\s+([0-9]+|[0-9]+\.[0-9]+)\sseconds/';

$str = `./check_http $Cache::nullhost -wt 1 -ct 2`;
$t += ok $?>>8,2;

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
