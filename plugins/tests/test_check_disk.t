#!/usr/bin/perl
use Test::More;
if (! -e "./test_check_disk") {
	plan skip_all => "./test_check_disk not compiled - please enable libtap library to test";
}
exec "./test_check_disk";
