#! /usr/bin/perl -w

use strict;
use Helper;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 2; plan tests => $tests}

exit(0) unless (-x "./check_mysql");

my $null = '';
my $cmd;
my $str;
my $t;

my $mysqlserver = get_option("mysqlserver","host for MYSQL tests");

$cmd = "./check_mysql -H $mysqlserver -P 3306";
$str = `$cmd`;
$t += ok $?>>8,2;
$t += ok $str, '/Access denied for user: /';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
