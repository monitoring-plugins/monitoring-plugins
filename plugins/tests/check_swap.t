#!/usr/bin/perl
use strict;
use warnings;

use Test::More;
if (! -e "./tests/test_check_swap") {
    plan skip_all => "./test_check_swap not compiled - please enable libtap library to test";
}
exec "./tests/test_check_swap";
