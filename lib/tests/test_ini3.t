#!/usr/bin/perl
use Test::More;
use strict;
use warnings;

if (! -e "./test_ini3") {
	plan skip_all => "./test_ini not compiled - please enable libtap library and/or extra-opts to test";
}

# array of argument arrays
#   - First value is the expected return code
#   - 2nd value is the NAGIOS_CONFIG_PATH
#     TODO: looks like we look in default path after looking trough this variable - shall we?
#   - 3rd value is the plugin name
#   - 4th is the ini locator
my @TESTS = (
	[3, undef, "section", "section_unknown@./config-tiny.ini"],
);

plan tests => scalar(@TESTS);

my $count=1;

foreach my $args (@TESTS) {
	my $rc = shift(@$args);
	if (my $env = shift(@$args)) {
		$ENV{"NAGIOS_CONFIG_PATH"} = $env;
	} else {
		delete($ENV{"NAGIOS_CONFIG_PATH"});
	}
	system {'./test_ini3'} @$args;
	cmp_ok($?>>8, '==', $rc, "Parse-ini die " . $count++);
}

