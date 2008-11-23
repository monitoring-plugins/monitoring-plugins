#! /usr/bin/perl -w -I ..
#
# Remote Procedure Call (RPC) Tests via check_rpc
#
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 2; plan tests => $tests}

my $successOutput = '/^check_rpc/';

my $t;

$t += checkCmd( "./check_rpc -V",   0, $successOutput );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
