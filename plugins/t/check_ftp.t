#! /usr/bin/perl -w

#use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 3; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_ftp $Cache::hostname -wt 300 -ct 600";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/FTP OK -\s+[0-9]?\.?[0-9]+ second response time/';

#$cmd = "./check_ftp $Cache::noserver -wt 0 -ct 0";
#$str = `$cmd`;
#$t += ok $?>>8,2;
#print "Test was: $cmd\n" unless ($?);

$cmd = "./check_ftp $Cache::nullhost -wt 0 -ct 0 -to 1";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
