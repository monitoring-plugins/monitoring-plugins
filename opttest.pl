#!/usr/bin/perl -w
use strict;
use Test;

use vars qw($dir $file $prog $idx $state $output %progs @dirs);

my $tests = 0;

@dirs = qw(plugins plugins-scripts);

foreach $dir (@dirs) {
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

for $prog (keys %progs) {
	$state = 0;
	$file = `basename $prog`;

	$idx = 1;
	$output = `$prog -h 2>&1`;
	if($?) {$state++;print "$prog failed test $idx\n";}
	unless ($output =~ m/$progs{$prog}/ms) {
		$idx++; $state++;print "$output\n$prog failed test $idx\n";
	}

	$idx++;
	`$prog --help 2>&1 > /dev/null`;
	if($?) {$state++;print "$prog failed test $idx\n";}

	$idx++;
		`$prog -V 2>&1 > /dev/null`;
	if($?) {$state++;print "$prog failed test $idx\n";}

	$idx++;
	`$prog --version 2>&1 > /dev/null`;
	if($?) {$state++;print "$prog failed test $idx\n";}

	print "$prog ($idx tests) ";
	ok $state,0;
}

