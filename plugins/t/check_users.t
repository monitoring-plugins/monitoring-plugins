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

$cmd = "./check_users 1000 1000";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^USERS OK - +[0-9]+ users currently logged in$/';

$cmd = "./check_users 0 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^USERS CRITICAL - [0-9]+ +users currently logged in$/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
