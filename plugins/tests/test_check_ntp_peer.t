#!/usr/bin/perl

use strict;
use warnings;
use Test::More;

my $test_file_path = "tests/test_check_ntp_peer";

if (! -e $test_file_path) {
	plan skip_all => $test_file_path." not compiled - please enable libtap library to test";
}
exec $test_file_path;
