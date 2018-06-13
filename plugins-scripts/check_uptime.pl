#!@PERL@ -w

# check_uptime - check uptime to see how long the system is running.
#

# License Information:
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
# USA
#
############################################################################

use POSIX;
use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_v $verbose $PROGNAME $opt_w $opt_c
					$opt_f $opt_s
					$status $state $msg);
use FindBin;
use lib "$FindBin::Bin";
use utils qw(%ERRORS &print_revision &support &usage );

sub print_help ();
sub print_usage ();
sub process_arguments ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';
$PROGNAME = "check_uptime";
$state = $ERRORS{'UNKNOWN'};

my $uptime_file = "/proc/uptime";


# Process arguments

Getopt::Long::Configure('bundling');
$status = process_arguments();
if ($status){
	print "ERROR: processing arguments\n";
	exit $ERRORS{"UNKNOWN"};
}


# Get uptime info from file

if ( ! -r $uptime_file ) {
	print "ERROR: file '$uptime_file' is not readable\n";
	exit $ERRORS{"UNKNOWN"};
}

if ( ! open FILE, "<", $uptime_file ) {
	print "ERROR: cannot read from file '$uptime_file'\n";
	exit $ERRORS{"UNKNOWN"};
}

chomp( my $file_content = <FILE> );
close FILE;

print "$uptime_file: $file_content\n" if $verbose;

# Get first digit value (without fraction)
my ( $uptime_seconds ) = $file_content =~ /^([\d]+)/;

# Bail out if value is not numeric
if ( $uptime_seconds !~ /^\d+$/ ) {
	print "ERROR: no numeric value: $uptime_seconds\n";
	exit $ERRORS{"UNKNOWN"};
}


# Do calculations for a "pretty" format (2 weeks, 5 days, ...)

my ( $secs, $mins, $hours, $days, $weeks );
$secs = $uptime_seconds;
$mins = $hours = $days = $weeks = 0;
if ( $secs > 100 ) {
	$mins = int( $secs / 60 );
	$secs -= $mins * 60;
}
if ( $mins > 100 ) {
	$hours = int( $mins / 60 );
	$mins -= $hours * 60;
}
if ( $hours > 48 ) {
	$days = int( $hours / 24 );
	$hours -= $days * 24;
}
if ( $days > 14 ) {
	$weeks = int( $days / 7 );
	$days -= $weeks * 7;
}

my $pretty_uptime = "";
$pretty_uptime .= sprintf( "%d week%s, ",   $weeks, $weeks == 1 ? "" : "s" )  if  $weeks;
$pretty_uptime .= sprintf( "%d day%s, ",    $days, $days == 1 ? "" : "s" )    if  $days;
$pretty_uptime .= sprintf( "%d hour%s, ",   $hours, $hours == 1 ? "" : "s" )  if  $hours;
$pretty_uptime .= sprintf( "%d minute%s, ", $mins, $mins == 1 ? "" : "s" )    if  $mins;
# Replace last occurence of comma with "and"
$pretty_uptime =~ s/, $/ and /;
# Always print the seconds (though it may be 0 seconds)
$pretty_uptime .= sprintf( "%d second%s", $secs, $secs == 1 ? "" : "s" );


# Default to catch errors in program
my $state_str = "UNKNOWN";

# Check values
if ( $uptime_seconds > $opt_c ) {
	$state_str = "CRITICAL";
} elsif ( $uptime_seconds > $opt_w ) {
	$state_str = "WARNING";
} else {
	$state_str = "OK";
}

$msg = "$state_str: ";

$msg .= "uptime is $uptime_seconds seconds. ";
$msg .= "Running for $pretty_uptime. "  if  $opt_f;
if ( $opt_s ) {
	chomp( my $up_since = `uptime -s` );
	$msg .= "Running since $up_since. ";
}

$state = $ERRORS{$state_str};

# Perfdata support
print "$msg|uptime=${uptime_seconds}s;$opt_w;$opt_c;0\n";
exit $state;


#####################################
#### subs


sub process_arguments(){
	GetOptions
		("V"   => \$opt_V, "version"	=> \$opt_V,
		 "v"   => \$opt_v, "verbose"	=> \$opt_v,
		 "h"   => \$opt_h, "help"		=> \$opt_h,
		 "w=s" => \$opt_w, "warning=s"  => \$opt_w,   # warning if above this number
		 "c=s" => \$opt_c, "critical=s" => \$opt_c,	  # critical if above this number
		 "f"   => \$opt_f, "for"        => \$opt_f,	  # show "running for ..."
		 "s"   => \$opt_s, "since"      => \$opt_s,	  # show "running since ..."
		 );

	if ($opt_V) {
		print_revision($PROGNAME,'@NP_VERSION@');
		exit $ERRORS{'UNKNOWN'};
	}

	if ($opt_h) {
		print_help();
		exit $ERRORS{'UNKNOWN'};
	}

	if (defined $opt_v) {
		$verbose = $opt_v;
	}

	unless ( defined $opt_w && defined $opt_c ) {
		print_usage();
		exit $ERRORS{'UNKNOWN'};
	}

	# Check if suffix is present
	# Calculate parameter to seconds (to get an integer value finally)
	# s = seconds
	# m = minutes
	# h = hours
	# d = days
	# w = weeks
	my %factor = ( "s" => 1,
		       "m" => 60,
		       "h" => 60 * 60,
		       "d" => 60 * 60 * 24,
		       "w" => 60 * 60 * 24 * 7,
		     );
	if ( $opt_w =~ /^(\d+)([a-z])$/ ) {
		my $value = $1;
		my $suffix = $2;
		print "warning: value=$value, suffix=$suffix\n" if $verbose;
		if ( ! defined $factor{$suffix} ) {
			print "Error: wrong suffix ($suffix) for warning";
			exit $ERRORS{'UNKNOWN'};
		}
		$opt_w = $value * $factor{$suffix};
	}
	if ( $opt_c =~ /^(\d+)([a-z])$/ ) {
		my $value = $1;
		my $suffix = $2;
		print "critical: value=$value, suffix=$suffix\n" if $verbose;
		if ( ! defined $factor{$suffix} ) {
			print "Error: wrong suffix ($suffix) for critical";
			exit $ERRORS{'UNKNOWN'};
		}
		$opt_c = $value * $factor{$suffix};
	}

	if ( $opt_w !~ /^\d+$/ ) {
		print "Warning (-w) is not numeric\n";
		exit $ERRORS{'UNKNOWN'};
	}
	if ( $opt_c !~ /^\d+$/ ) {
		print "Critical (-c) is not numeric\n";
		exit $ERRORS{'UNKNOWN'};
	}

	if ( $opt_w >= $opt_c) {
		print "Warning (-w) cannot be greater than Critical (-c)!\n";
		exit $ERRORS{'UNKNOWN'};
	}

	return $ERRORS{'OK'};
}

sub print_usage () {
	print "Usage: $PROGNAME -w <warn> -c <crit> [-v]\n";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2002 Subhendu Ghosh/Carlos Canau/Benjamin Schmid\n";
	print "Copyright (c) 2018 Bernd Arnold\n";
	print "\n";
	print_usage();
	print "\n";
	print "   Checks the uptime of the system using $uptime_file\n";
	print "\n";
	print "-w (--warning)   = Min. number of uptime to generate warning\n";
	print "-c (--critical)  = Min. number of uptime to generate critical alert ( w < c )\n";
	print "-f (--for)       = Show uptime in a pretty format (Running for x weeks, x days, ...)\n";
	print "-s (--since)     = Show last boot in yyyy-mm-dd HH:MM:SS format (output from 'uptime -s')\n";
	print "-h (--help)\n";
	print "-V (--version)\n";
	print "-v (--verbose)   = debugging output\n";
	print "\n\n";
	print "Note: -w and -c are required arguments.\n";
	print "      You can suffix both values with s for seconds (default), m (minutes), h (hours), d (days) or w (weeks).\n";
	print "";
	print "\n\n";
	support();
}
