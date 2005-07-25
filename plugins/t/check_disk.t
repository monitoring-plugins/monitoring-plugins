#! /usr/bin/perl -w -I ..
#
# Disk Space Tests via check_disk
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 10; plan tests => $tests}

my $successOutput = '/^DISK OK - /';
my $failureOutput = '/^DISK CRITICAL - /';

my $mountpoint_valid   = getTestParameter( "mountpoint_valid",   "NP_MOUNTPOINT_VALID",   "/",
					   "The path to a valid mountpoint" );

my $mountpoint_invalid = getTestParameter( "mountpoint_invalid", "NP_MOUNTPOINT_INVALID", "/missing",
					   "The path to a invalid (non-existent) mountpoint" );

my $t;

$t += checkCmd( "./check_disk 100 100       ${mountpoint_valid}",   0, $successOutput );
$t += checkCmd( "./check_disk -w 0 -c 0     ${mountpoint_valid}",   0, $successOutput );
$t += checkCmd( "./check_disk -w 1\% -c 1\% ${mountpoint_valid}",   0, $successOutput );
$t += checkCmd( "./check_disk 0 0           ${mountpoint_valid}",   2, $failureOutput );
$t += checkCmd( "./check_disk 100 100       ${mountpoint_invalid}", 2, '/not found/'  );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
