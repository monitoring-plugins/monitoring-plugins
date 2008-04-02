#!/usr/bin/perl
use Test::More;
if (! -e "./test_opts") {
	plan skip_all => "./test_opts not compiled - please install tap library and/or enable parse-ini to test";
}
exec "./test_opts";
