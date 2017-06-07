#!/usr/bin/perl
use Test::More;
use strict;
use warnings;

if (! -e "./test_opts3") {
	plan skip_all => "./test_opts3 not compiled - please enable libtap library and/or extra-opts to test";
}

# array of argument arrays
#   - First value is the expected return code
#   - 2nd value is the NAGIOS_CONFIG_PATH
#     TODO: looks like we look in default path after looking trough this variable - shall we?
#   - 3rd value is the plugin name
#   - 4th and up are arguments
my @TESTS = (
	[3, '/nonexistent', 'prog_name', 'arg1', '--extra-opts', '--arg3', 'val2'],
	[3, '.', 'prog_name', 'arg1', '--extra-opts=missing@./config-opts.ini', '--arg3', 'val2'],
	[3, '', 'prog_name', 'arg1', '--extra-opts', 'missing@./config-opts.ini', '--arg3', 'val2'],
	[3, '.', 'check_missing', 'arg1', '--extra-opts=@./config-opts.ini', '--arg3', 'val2'],
	[3, '.', 'check_missing', 'arg1', '--extra-opts', '--arg3', 'val2'],
	[0, '/tmp:/var:/nonexistent:.', 'check_tcp', 'arg1', '--extra-opts', '--arg3', 'val2'],
	[0, '/usr/local/nagios/etc:.:/etc', 'check_missing', 'arg1', '--extra-opts=check_tcp', '--arg3', 'val2'],
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
	system {'./test_opts3'} @$args;
	cmp_ok($?>>8, '==', $rc, "Extra-opts die " . $count++);
}

