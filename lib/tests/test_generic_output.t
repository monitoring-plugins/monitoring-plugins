#!/usr/bin/perl
use Test::More;
if (! -e "./test_generic_output") {
	plan skip_all => "./test_generic_output not compiled - please enable libtap library to test";
}
exec "./test_generic_output";
