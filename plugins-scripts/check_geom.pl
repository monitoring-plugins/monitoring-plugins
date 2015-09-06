#!/usr/bin/perl -w

# Copyright (c) 2007, 2008 
# Written by Nathan Butcher
#
# Released under the GNU Public License
#
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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Version: 1.4
# This plugin currently supports : 
# "mirror", "stripe", "raid3", "concat", and "shsec" GEOM classes.
# With a bit of fondling, it could be expanded to recognize other classes
#
# Selecting a specific volume to monitor is not mandatory. Should this
# argument be left out, this plugin will show results from all volumes of
# the geom class you select.
#
# Usage:   check_geom <geom class> [volume]
#
# Example: check_geom mirror gm0
#          WARNING gm0 DEGRADED, { ad0 , ad1 (32%) }

use strict;

my %ERRORS=('DEPENDENT'=>4,'UNKNOWN'=>3,'OK'=>0,'WARNING'=>1,'CRITICAL'=>2);
my $state="UNKNOWN";
my $msg="FAILURE";

if ($#ARGV < 0) {
	print "Not enough arguments!\nUsage: $0 <class> [device]\n";
	exit $ERRORS{$state};
}

if ($^O ne 'freebsd') {
	print "This plugin is only applicable on FreeBSD.\n";
	exit $ERRORS{$state};
}

my $class=$ARGV[0];
my $volume="";
my $regex='^\s*' . $class . '\/';

if ($ARGV[1]) {
	$volume="$ARGV[1]";
	$regex = $regex . $volume . '\s';
}

my $statcommand="geom $class status";
if (! open STAT, "$statcommand|") {
	print ("$state $statcommand returns no result!");
	exit $ERRORS{$state};
}

my $found=0;
my $exist=0;
my $status="";
my $stateflag=0;
my $name="";
my $compo="";
my $compoflag=0;
my $output="";

sub endunit {

	$found=0;
	$output = "$output - $name $status { $compo }";

	if ($class eq "mirror" && $status !~ /COMPLETE/ ) {
		if ($compoflag >= 2 && $state ne "CRITICAL") {
			$state = "WARNING";
		} else {
			$state = "CRITICAL";
		}
	}

	if ($class eq "raid3" && $status !~ /COMPLETE/ ) {

		$statcommand="geom $class list $volume";
		my $unit=0;
		
		if (! open STAT, "$statcommand|") {
			print ("$state $statcommand returns no result!");
			exit $ERRORS{$state};
		}

		while (<STAT>) {
			next unless (/Components:/);
			($unit) = /([0-9]+)$/;
			next;
		}

		if ($compoflag == $unit && $state ne "CRITICAL") {
			$state = "WARNING";
		}
	}

	$compoflag = 0;

}

while(<STAT>) {

	chomp;
	if ($found) {
		if (/^\s*$class\//) {
			&endunit;	
		} else {
			my ($vgh) = /\s+(.*)/;
			$compo="$compo , $vgh";
			$compoflag++;
		}
	}

	if (/$regex/) {
		($name, $status, $compo) = /^\s*(\S+)\s+(\S+)\s+(.*)$/;
		$found=1;
		$exist++;
		$compoflag++;

		if (($class eq "mirror" || $class eq "raid3") && $status =~ /COMPLETE/ ) {
			$stateflag++;
		}

		if ($class eq "stripe" || $class eq "concat" || $class eq "shsec" && $status =~ /UP/) {
			$stateflag++;
		}
	}
	
}
if ($found != 0) {
	&endunit;
}
close(STAT);

if (! $exist ) {
	$state = "CRITICAL";
	if (! $volume) {
		$volume = "volumes";
	}
	$msg = sprintf "%s %s unavailable, non-existant, or not responding!\n", $class, $volume;
	print $state, " ", $msg;
	exit ($ERRORS{$state});
}

if ($state eq "UNKNOWN" && $stateflag < $exist) {
	$state = "CRITICAL";
}

if ($state eq "UNKNOWN") {
	$state = "OK";
}

#goats away!
$msg = sprintf "%s %s\n", $class, $output;
print $state, " ", $msg;
exit ($ERRORS{$state});
