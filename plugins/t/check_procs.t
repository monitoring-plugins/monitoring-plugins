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
$cmd = "./check_procs -w 100000 -c 100000";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^PROCS OK: [0-9]+ process(es)?$/';

# Reverse Compatibility
$cmd = "./check_procs -w 100000 -c 100000 -s Z";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^PROCS OK: [0-9]+ process(es)? with /';

# Reverse Compatibility
$cmd = "./check_procs -w 0 -c 10000000";
$str = `$cmd`;
$t += ok $?>>8,1;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^PROCS WARNING: [0-9]+ process(es)?$/';

# Reverse Compatibility
$cmd = "./check_procs -w 0 -c 0";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^PROCS CRITICAL: [0-9]+ process(es)?$/';

# Reverse Compatibility
$cmd = "./check_procs -w 0 -c 0 -s S";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^PROCS CRITICAL: [0-9]+ process(es)? with /';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
