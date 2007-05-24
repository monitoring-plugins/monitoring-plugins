#! /usr/bin/perl -w -I ..
#
# negate checks
# Need check_dummy to work for testing
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

plan tests => 40;

my $res;

$res = NPTest->testCmd( "./negate" );
is( $res->return_code, 3, "Not enough parameters");
like( $res->output, "/Could not parse arguments/", "Could not parse arguments");

$res = NPTest->testCmd( "./negate ./check_dummy 0 'a dummy okay'" );
is( $res->return_code, 2, "OK changed to CRITICAL" );
is( $res->output, "OK: a dummy okay" );

$res = NPTest->testCmd( "./negate './check_dummy 0 redsweaterblog'");
is( $res->return_code, 2, "OK => CRIT with a single quote for command to run" );
is( $res->output, "OK: redsweaterblog" );

$res = NPTest->testCmd( "./negate ./check_dummy 1 'a warn a day keeps the managers at bay'" );
is( $res->return_code, 2, "WARN stays same" );

$res = NPTest->testCmd( "./negate ./check_dummy 3 mysterious");
is( $res->return_code, 3, "UNKNOWN stays same" );

my %state = (
	ok => 0,
	warning => 1,
	critical => 2,
	unknown => 3,
	);
foreach my $current_state (qw(ok warning critical unknown)) {
	foreach my $new_state (qw(ok warning critical unknown)) {
		$res = NPTest->testCmd( "./negate --$current_state=$new_state ./check_dummy ".$state{$current_state}." 'Fake $new_state'" );
		is( $res->return_code, $state{$new_state}, "Got fake $new_state" );
		is( $res->output, uc($current_state).": Fake $new_state" );
	}
}

