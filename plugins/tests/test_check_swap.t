#!/usr/bin/perl
use Test::More;
if (! -e "./test_check_swap") {
    plan skip_all => "./test_swap not compiled - please enable libtap library to test";
}
exec "./test_check_swap";
