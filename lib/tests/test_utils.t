#!/usr/bin/perl
use Test::More;
if (! -e "./test_utils") {
	plan skip_all => "./test_utils not compiled - please enable libtap library to test";
}
exec "./test_utils";
