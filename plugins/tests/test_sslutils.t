#!/usr/bin/perl
use Test::More;
if (! -e "./test_sslutils") {
	plan skip_all => "./test_sslutils not compiled - please enable libtap library to test";
}
exec "./test_sslutils";
