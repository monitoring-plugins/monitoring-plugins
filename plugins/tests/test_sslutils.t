#!/usr/bin/perl
use Test::More;
if (! -e "./tests/test_sslutils") {
	plan skip_all => "./tests/test_sslutils not compiled - please enable libtap library to test";
}
exec "./tests/test_sslutils";
