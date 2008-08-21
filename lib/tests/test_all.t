#!/usr/bin/perl
# Creates $file.t for each @ARGV
# Then calls runtests for all these files

use strict;
use Test::Harness;
use Getopt::Std;

my $opts = {};
getopts("v", $opts) or die "Getopt failed";

$Test::Harness::verbose = $opts->{v};
$Test::Harness::switches="";

my $special_errors = {
	test_ini  => "please enable parse-ini to test",
	test_opts => "please enable parse-ini to test",
};
my $default_error = "could not compile";

my @tests;
foreach my $file (@ARGV) {
	my $file_t = "$file.t";
	my $error = $special_errors->{ $file } || $default_error;
	open F, ">", $file_t or die "Cannot open $file_t for writing";
	print F <<EOF;
use Test::More;
if (! -e "$file") {
	plan skip_all => "./$file not compiled - $error";
}
exec "./$file";
EOF
	close F;
	push @tests, $file_t;
}
chmod 0750, @tests;
runtests @tests;
unlink @tests;
