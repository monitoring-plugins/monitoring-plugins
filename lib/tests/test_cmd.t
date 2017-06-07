#!/usr/bin/perl
use Test::More;
if (! -e "./test_cmd") {
	plan skip_all => "./test_cmd not compiled - please enable libtap library to test";
}
exec "./test_cmd";
