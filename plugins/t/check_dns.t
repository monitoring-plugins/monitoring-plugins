#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

#`nslookup localhost > /dev/null 2>&1` || exit(77);

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_dns 127.0.0.1 -to 5";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/DNS ok - +[\.0-9]+ seconds response time, Address\(es\) is\/are /';

$cmd = "./check_dns $Cache::nullhost -to 1";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
