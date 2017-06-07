#!/usr/bin/perl
use Test::More;
if (! -e "./test_ini1") {
	plan skip_all => "./test_ini not compiled - please enable libtap library and/or extra-opts to test";
}
exec "./test_ini1";
