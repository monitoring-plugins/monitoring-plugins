#!/usr/bin/perl
use Test::More;
if (! -e "./test_opts2") {
	plan skip_all => "./test_opts2 not compiled - please install tap library and/or enable parse-ini to test";
}
$ENV{"NAGIOS_CONFIG_PATH"} = ".";
exec "./test_opts2";
