#! /usr/bin/perl -w -I ..
#
# Swap Space Tests via check_swap
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 6; plan tests => $tests}

my $t;

my $successOutput = '/^SWAP OK - [0-9]+\% free \([0-9]+ MB out of [0-9]+ MB\)/';
my $failureOutput = '/^SWAP CRITICAL - [0-9]+\% free \([0-9]+ MB out of [0-9]+ MB\)/';

$t += checkCmd( "./check_swap -w 1048576 -c 1048576", 0, $successOutput ); # 1MB  free
$t += checkCmd( "./check_swap -w   1\%   -c     1\%", 0, $successOutput ); # 1%   free
$t += checkCmd( "./check_swap -w 100\%   -c   100\%", 2, $failureOutput ); # 100% free (always fails)

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
