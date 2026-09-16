#!/usr/bin/perl
use strict;
use warnings;

use Test::More;
if (! -e "./tests/test_check_snmp") {
	plan skip_all => "./tests/test_check_snmp not compiled - please enable libtap library to test";
}
exec "./tests/test_check_snmp";
