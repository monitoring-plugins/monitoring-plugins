#!/usr/bin/perl -w
use strict;
use warnings;
use Test;

use vars qw($dir $file $prog $idx $state $output %progs @dirs);

my $tests = 0;

@dirs = qw(plugins plugins-scripts);

foreach my $dir (@dirs) {
	opendir(DIR, $dir) || die "can't opendir $dir: $!";
	while ($file = readdir(DIR)) {
		if (-x "$dir/$file" && -f "$dir/$file") {
			$tests++;
			$progs{"$dir/$file"} = $file;
		}
	}
	closedir DIR;
}

plan tests => $tests;

for my $prog (keys %progs) {
	$state = 0;
	$file = `basename $prog`;

	$idx = 1;
	$output = `$prog -h 2>&1`;
	if(($? >> 8) != 3) {
		$state++;
		print "$prog failed test $idx (help exit code (short form))\n";
		exit(1);
	}

	unless ($output =~ m/$progs{$prog}/ms) {
		$idx++;
		$state++;
		print "$output\n$prog failed test $idx\n";
	}

	$idx++;
	`$prog --help 2>&1 > /dev/null`;
	if(($? >> 8) != 3) {
		$state++;
		print "$prog failed test $idx (help exit code (long form))\n";
		exit(1);
	}

	$idx++;
	`$prog -V 2>&1 > /dev/null`;
	if(($? >> 8) != 3) {
		$state++;
		print "$prog failed test $idx (version exit code (short form))\n";
		exit(1);
	}

	$idx++;
	`$prog --version 2>&1 > /dev/null`;
	if(($? >> 8) != 3) {
		$state++;
		print "$prog failed test $idx (version exit code (long form))\n";
		exit(1);
	}

	print "$prog ($idx tests) ";
	ok $state,0;
}

