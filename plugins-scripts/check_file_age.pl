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
$opt_W = 0;
$opt_C = 0;
$opt_f = "";

Getopt::Long::Configure('bundling');
GetOptions(
	"V"   => \$opt_V, "version"	=> \$opt_V,
	"h"   => \$opt_h, "help"	=> \$opt_h,
	"i"   => \$opt_i, "ignore-missing"	=> \$opt_i,
	"f=s" => \$opt_f, "file"	=> \$opt_f,
	"w=f" => \$opt_w, "warning-age=f" => \$opt_w,
	"W=f" => \$opt_W, "warning-size=f" => \$opt_W,
	"c=f" => \$opt_c, "critical-age=f" => \$opt_c,
	"C=f" => \$opt_C, "critical-size=f" => \$opt_C);

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
$perfdata = "age=${age}s;${opt_w};${opt_c} size=${size}B;${opt_W};${opt_C};0";


$result = 'OK';

if (($opt_c and $age > $opt_c) or ($opt_C and $size < $opt_C)) {
	$result = 'CRITICAL';
}
elsif (($opt_w and $age > $opt_w) or ($opt_W and $size < $opt_W)) {
	$result = 'WARNING';
}

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
	print "  <size>  File must be at least this many bytes long (default: crit 0 bytes)\n";
	print "\n";
	support();
}
