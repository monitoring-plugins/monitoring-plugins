#! /usr/bin/perl -w
# $Id$

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

exit(0) unless (-x "./check_fping");

#`fping 127.0.0.1 > /dev/null 2>&1` || exit(77);

my $null = '';
my $cmd;
my $str;
my $t;
my $stat;


$cmd = "./check_fping 127.0.0.1";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^FPING OK - 127.0.0.1/';

$cmd = "./check_fping $Cache::nullhost";
$str = `$cmd`;
if ($?>>8 == 1 or $?>>8 == 2) {
	$stat = 2;
}
$t += ok $stat,2;
print "Test was: $cmd\n" if (($?>>8) < 1);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
