#! /usr/bin/perl -w

use strict;
use Cache;
use Test;
use vars qw($tests);

BEGIN {$tests = 5; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_ping 127.0.0.1 100 100 1000 1000 -p 1";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/PING (ok|OK) - Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';

$cmd = "./check_ping 127.0.0.1 0 0 0 0 -p 1";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/Packet loss = +[0-9]{1,2}\%, +RTA = [\.0-9]+ ms/';

$cmd = "./check_ping $Cache::nullhost 0 0 0 0 -p 1 -to 1";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
