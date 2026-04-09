#!/usr/bin/perl
use strict;
use warnings;

use Test::More;
if (! -e "./tests/test_check_disk") {
	plan skip_all => "./test_check_disk not compiled - please enable libtap library to test";
}
system("./tests/test_check_disk");
