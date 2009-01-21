#!/usr/bin/perl
use Test::More;
use strict;
use warnings;

if (! -e "./test_opts3") {
	plan skip_all => "./test_opts3 not compiled - please install tap library and/or enable parse-ini to test";
}

# array of argument arrays
#   - first value is the NAGIOS_CONFIG_PATH
#   - 2nd value is the plugin name
#   - 3rc and up are arguments
my @TESTS = (
	['/nonexistent', 'prog_name', 'arg1', '--extra-opts', '--arg3', 'val2'],
	['.', 'prog_name', 'arg1', '--extra-opts=missing@./config-opts.ini', '--arg3', 'val2'],
	['.', 'prog_name', 'arg1', '--extra-opts', 'missing@./config-opts.ini', '--arg3', 'val2'],
	['.', 'check_missing', 'arg1', '--extra-opts=@./config-opts.ini', '--arg3', 'val2'],
	['.', 'check_missing', 'arg1', '--extra-opts', '--arg3', 'val2'],
);

plan tests => scalar(@TESTS);

my $count=1;

foreach my $args (@TESTS) {
  $ENV{"NAGIOS_CONFIG_PATH"} = shift(@$args);
	system {'./test_opts3'} @$args;
	cmp_ok($?>>8, '==', 3, "Extra-opts die " . $count++);
}

