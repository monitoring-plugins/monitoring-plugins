#!/bin/perl -w

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
# along with this program (or with Nagios);  if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA

use strict;
use English;
use Getopt::Long;
use File::stat;
use vars qw($PROGNAME);
use lib ".";
use utils qw (%ERRORS &print_revision &support);

sub print_help ();
sub print_usage ();

my ($opt_c, $opt_f, $opt_m, $opt_w, $opt_C, $opt_W, $opt_h, $opt_V);
my ($result, $message, $age, $size, $st);

$PROGNAME="check_file_age";

$opt_w = 240;
$opt_c = 600;
$opt_W = 0;
$opt_C = 0;
$opt_f = "";

Getopt::Long::Configure('bundling');
GetOptions(
	"V"   => \$opt_V, "version"	=> \$opt_V,
	"h"   => \$opt_h, "help"	=> \$opt_h,
	"m"   => \$opt_m, "missing"	=> \$opt_m,
	"f=s" => \$opt_f, "file"	=> \$opt_f,
	"w=f" => \$opt_w, "warning-age=f" => \$opt_w,
	"W=f" => \$opt_W, "warning-size=f" => \$opt_W,
	"c=f" => \$opt_c, "critical-age=f" => \$opt_c,
	"C=f" => \$opt_C, "critical-size=f" => \$opt_C);

if ($opt_V) {
	print_revision($PROGNAME, '@NP_VERSION@');
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

$opt_f = shift unless ($opt_f);

if (! $opt_f) {
	print "FILE_AGE UNKNOWN: No file specified\n";
	exit $ERRORS{'UNKNOWN'};
}

# Check that file exists (can be directory or link)
unless (-e $opt_f) {
	# If we allow missing files/directories, return OK
	if ($opt_m) {
		print "FILE_AGE OK: File not found - $opt_f\n";
		exit $ERRORS{'OK'};
	} else {
		print "FILE_AGE CRITICAL: File not found - $opt_f\n";
		exit $ERRORS{'CRITICAL'};
	}
}

$st = File::stat::stat($opt_f);
$age = time - $st->mtime;
$size = $st->size;


$result = 'OK';

if (($opt_c and $age > $opt_c) or ($opt_C and $size < $opt_C)) {
	$result = 'CRITICAL';
}
elsif (($opt_w and $age > $opt_w) or ($opt_W and $size < $opt_W)) {
	$result = 'WARNING';
}

print "FILE_AGE $result: $opt_f is $age seconds old and $size bytes\n";
exit $ERRORS{$result};

sub print_usage () {
	print "Usage:\n";
	print "  $PROGNAME [-w <secs>] [-c <secs>] [-W <size>] [-C <size>] -f <file>\n";
	print "  $PROGNAME [-h | --help]\n";
	print "  $PROGNAME [-V | --version]\n";
}

sub print_help () {
	print_revision($PROGNAME, '@NP_VERSION@');
	print "Copyright (c) 2003 Steven Grimm\n\n";
	print_usage();
	print "\n";
	print "  <secs>  File must be no more than this many seconds old (default: warn 240 secs, crit 600)\n";
	print "  <size>  File must be at least this many bytes long (default: crit 0 bytes)\n";
	print "\n";
	support();
}
