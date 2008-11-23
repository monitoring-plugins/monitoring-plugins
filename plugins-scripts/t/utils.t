#!/usr/bin/perl -w -I ..
#
# utils.pm tests
#
#
# Run with perl t/utils.t

use warnings;
use strict;
use Test::More;
use NPTest;

use lib "..";
use utils;

my $hostname_checks = {
	"www.altinity.com" => 1,
	"www.888.com" => 1,
	"888.com" => 1,
	"host-hyphened.com" => 1,
	"rubbish" => 1,
	"-start.com" => 0,
	"nonfqdn-but-endsindot." => 1,
	"fqdn.and.endsindot." => 1,
	"lots.of.dots.dot.org" => 1,
	"endingwithdoubledots.." => 0,
	"toomany..dots" => 0,
	".start.with.dot" => 0,
	"10.20.30.40" => 1,
	"10.20.30.40.50" => 0,
	"10.20.30" => 0,
	"10.20.30.40." => 1,	# This is considered a hostname because of trailing dot. It probably won't exist though...
	"888." => 1,		# This is because it could be a domain
	"host.888." => 1,
	"where.did.that.!.come.from." => 0,
	"no.underscores_.com" => 0,
	"a.somecompany.com" => 1,
	"host.a.com" => 1,
	};

plan tests => ((scalar keys %$hostname_checks) + 4);

foreach my $h (sort keys %$hostname_checks) {
	is (utils::is_hostname($h), $hostname_checks->{$h}, "$h should return ".$hostname_checks->{$h});
}

is(utils::is_hostname(), 0, "No parameter errors");
is(utils::is_hostname(""), 0, "Empty string errors");
is(utils::is_hostname(0), 0, "0 also errors");
is(utils::is_hostname(1), 0, "1 also errors");
