#!/usr/bin/perl
use Test::More;
if (! -e "./test_disk") {
	plan skip_all => "./test_disk not compiled - please enable libtap library to test";
}
exec "./test_disk";
