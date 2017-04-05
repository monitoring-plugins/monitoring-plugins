#!@PERL@ -w

# check_file_age.pl Copyright (C) 2003 Steven Grimm <koreth-nagios@midwinter.com>
#
# Checks a file's size and modification time to make sure it's not empty
# and that it's sufficiently recent.
#
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty
# of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# you should have received a copy of the GNU General Public License
# along with this program if not, write to the Free Software Foundation,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA

use strict;
use English;
use Getopt::Long;
use File::stat;
use vars qw($PROGNAME);
use FindBin;
use lib "$FindBin::Bin";
use utils qw (%ERRORS &print_revision &support);

sub print_help ();
sub print_usage ();

my ($opt_c, $opt_f, $opt_w, $opt_C, $opt_W, $opt_h, $opt_V, $opt_i);
my ($result, $message, $age, $size, $st, $perfdata);

$PROGNAME="check_file_age";

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

$opt_w = 240;
$opt_c = 600;
$opt_f = "";

Getopt::Long::Configure('bundling');
GetOptions(
	"V"   => \$opt_V, "version"	=> \$opt_V,
	"h"   => \$opt_h, "help"	=> \$opt_h,
	"i"   => \$opt_i, "ignore-missing"	=> \$opt_i,
	"f=s" => \$opt_f, "file"	=> \$opt_f,
	"w=s" => \$opt_w, "warning-age=s" => \$opt_w,
	"W=s" => \$opt_W, "warning-size=s" => \$opt_W,
	"c=s" => \$opt_c, "critical-age=s" => \$opt_c,
	"C=s" => \$opt_C, "critical-size=s" => \$opt_C);

if ($opt_V) {
	print_revision($PROGNAME, '@NP_VERSION@');
	exit $ERRORS{'UNKNOWN'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'UNKNOWN'};
}

$opt_f = shift unless ($opt_f);

if (! $opt_f) {
	print "FILE_AGE UNKNOWN: No file specified\n";
	exit $ERRORS{'UNKNOWN'};
}

# Check that file exists (can be directory or link)
unless (-e $opt_f) {
	if ($opt_i) {
		$result = 'OK';
		print "FILE_AGE $result: $opt_f doesn't exist, but ignore-missing was set\n";
		exit $ERRORS{$result};

	} else {
		print "FILE_AGE CRITICAL: File not found - $opt_f\n";
		exit $ERRORS{'CRITICAL'};
	}
}

$st = File::stat::stat($opt_f);
$age = time - $st->mtime;
$size = $st->size;

$result = 'OK';

if ($opt_c !~ m/^\d+$/ or ($opt_C and $opt_C !~ m/^\d+$/)
		or $opt_w !~ m/^\d+$/ or ($opt_W and $opt_W !~ m/^\d+$/)) {
	# range has been specified so use M::P::R to process
	require Monitoring::Plugin::Range;
	# use permissive range defaults for size when none specified
	$opt_W = "0:" unless ($opt_W);
	$opt_C = "0:" unless ($opt_C);

	if (Monitoring::Plugin::Range->parse_range_string($opt_c)
		->check_range($age) == 1) { # 1 means it raises an alert because it's OUTSIDE the range
			$result = 'CRITICAL';
	}
	elsif (Monitoring::Plugin::Range->parse_range_string($opt_C)
		->check_range($size) == 1) {
			$result = 'CRITICAL';
	}
	elsif (Monitoring::Plugin::Range->parse_range_string($opt_w)
		->check_range($age) == 1) {
			$result = 'WARNING';
	}
	elsif (Monitoring::Plugin::Range->parse_range_string($opt_W)
		->check_range($size) == 1) {
			$result = 'WARNING';
	}
}
else {
	# use permissive defaults for size when none specified
	$opt_W = 0 unless ($opt_W);
	$opt_C = 0 unless ($opt_C);
	if ($age > $opt_c or $size < $opt_C) {
		$result = 'CRITICAL';
	}
	elsif ($age > $opt_w or $size < $opt_W) {
		$result = 'WARNING';
	}
}

$perfdata = "age=${age}s;${opt_w};${opt_c} size=${size}B;${opt_W};${opt_C};0";
print "FILE_AGE $result: $opt_f is $age seconds old and $size bytes | $perfdata\n";
exit $ERRORS{$result};

sub print_usage () {
	print "Usage:\n";
	print "  $PROGNAME [-w <secs>] [-c <secs>] [-W <size>] [-C <size>] [-i] -f <file>\n";
	print "  $PROGNAME [-h | --help]\n";
	print "  $PROGNAME [-V | --version]\n";
}

sub print_help () {
	print_revision($PROGNAME, '@NP_VERSION@');
	print "Copyright (c) 2003 Steven Grimm\n\n";
	print_usage();
	print "\n";
	print "  -i | --ignore-missing :  return OK if the file does not exist\n";
	print "  <secs>  File must be no more than this many seconds old (default: warn 240 secs, crit 600)\n";
	print "  <size>  File must be at least this many bytes long (default: crit 0 bytes)\n\n";
	print "  Both <secs> and <size> can specify a range using the standard plugin syntax\n";
	print "  If any of the warning and critical arguments are in range syntax (not just bare numbers)\n";
	print "  then all warning and critical arguments will be interpreted as ranges.\n";
	print "  To use range processing the perl module Monitoring::Plugin must be installed\n";
	print "  For range syntax see https://www.monitoring-plugins.org/doc/guidelines.html#THRESHOLDFORMAT\n";
	print "  It is strongly recommended when using range syntax that all four of -w, -W, -c and -C are specified\n";
	print "  otherwise it is unlikely that the size test will be doing what is desired\n";
	print "\n";
	support();
}

