#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 10; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

# Reverse Compatibility
$cmd = "./check_procs 100000 100000";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^OK - [0-9]+ processes running$/';

# Reverse Compatibility
$cmd = "./check_procs 100000 100000 Z";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^OK - [0-9]+ processes running with /';

# Reverse Compatibility
$cmd = "./check_procs 0 10000000";
$str = `$cmd`;
$t += ok $?>>8,1;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^WARNING - [0-9]+ processes running$/';

# Reverse Compatibility
$cmd = "./check_procs 0 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL - [0-9]+ processes running$/';

# Reverse Compatibility
$cmd = "./check_procs 0 0 S";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^CRITICAL - [0-9]+ processes running with /';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
