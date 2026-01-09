#!/usr/bin/perl
use Test::More;
if (! -e "./test_check_snmp") {
	plan skip_all => "./test_check_snmp not compiled - please enable libtap library to test";
}
exec "./test_check_snmp";
