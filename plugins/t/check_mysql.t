#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 1; plan tests => $tests}

exit(0) unless (-x "./check_mysql");

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_mysql -H 127.0.0.1 -P 3306";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
