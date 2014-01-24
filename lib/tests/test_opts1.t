#!/usr/bin/perl
use Test::More;
if (! -e "./test_opts1") {
	plan skip_all => "./test_opts1 not compiled - please enable libtap library and/or extra-opts to test";
}
exec "./test_opts1";
