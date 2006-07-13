#!/usr/bin/perl
use Test::More;
if (! -e "./test_utils") {
	plan skip_all => "./test_utils not compiled - please install tap library to test";
}
exec "./test_utils";
