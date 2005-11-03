#! /usr/bin/perl -w -I ..
#
# Process Tests via check_procs
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 12; plan tests => $tests}

my $t;

$t += checkCmd( "./check_procs -w 100000 -c   100000",      0, '/^PROCS OK: [0-9]+ process(es)?$/' );
$t += checkCmd( "./check_procs -w 100000 -c   100000 -s Z", 0, '/^PROCS OK: [0-9]+ process(es)? with /' );
$t += checkCmd( "./check_procs -w      0 -c 10000000",      1, '/^PROCS WARNING: [0-9]+ process(es)?$/' );
$t += checkCmd( "./check_procs -w 0      -c        0",      2, '/^PROCS CRITICAL: [0-9]+ process(es)?$/' );
$t += checkCmd( "./check_procs -w 0      -c        0 -s S", 2, '/^PROCS CRITICAL: [0-9]+ process(es)? with /' );
$t += checkCmd( "./check_procs -w 0      -c 10000000 -p 1", 1, '/^PROCS WARNING: [0-9]+ process(es)? with PPID = 1/' );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
