#!/usr/bin/perl
use Test::More;
if (! -e "./test_utils") {
	plan skip_all => "./test_utils not compiled - check ./configure --with-libtap-object is defined";
}
exec "./test_utils";
