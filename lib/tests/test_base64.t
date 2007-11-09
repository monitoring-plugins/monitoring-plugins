#!/usr/bin/perl
use Test::More;
if (! -e "./test_base64") {
	plan skip_all => "./test_base64 not compiled - please install tap library to test";
}
exec "./test_base64";
