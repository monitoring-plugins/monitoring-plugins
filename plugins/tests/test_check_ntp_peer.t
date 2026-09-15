#!/usr/bin/perl
use Test::More;
if (! -e "./test_check_ntp_peer") {
	plan skip_all => "./test_check_ntp_peer not compiled - please enable libtap library to test";
}
exec "./test_check_ntp_peer";
