#!/usr/bin/perl -w -I ..
#
# utils.pm tests
#
# $Id$
#

#use strict;
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
	"endsindot." => 0,
	"lots.of.dots.dot.org" => 1,
	"10.20.30.40" => 1,
	"10.20.30.40.50" => 0,
	"10.20.30" => 0,
	};

plan tests => scalar keys %$hostname_checks;

foreach my $h (sort keys %$hostname_checks) {
	is (utils::is_hostname($h), $hostname_checks->{$h}, "$h should return ".$hostname_checks->{$h});
}

