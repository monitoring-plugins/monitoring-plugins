#!@PERL@ -w

# Copyright (c) 2007, 2008
# Written by Nathan Butcher
#
# Copyright (c) 2015
# Updated for monitoring-plugins by Dean Hamstead
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
# Version: 1.5
# This plugin currently supports :
# "mirror", "stripe", "raid3", "concat", and "shsec" GEOM classes.
# With a bit of fondling, it could be expanded to recognize other classes
#
# Selecting a specific volume to monitor is not mandatory. Should this
# argument be left out, this plugin will show results from all volumes of
# the geom class you select.
#
# Usage:   check_geom -c <geom class> [ -v <volume> ]
#
# Example: check_geom mirror gm0
#          WARNING gm0 DEGRADED, { ad0 , ad1 (32%) }

use strict;
use Getopt::Long qw( :config no_ignore_case );

use vars qw($PROGNAME $opt_V $opt_h $opt_class $opt_vol );
use FindBin;
use lib "$FindBin::Bin";
use lib '@libexecdir@';
use utils qw(%ERRORS &print_revision &support );

$PROGNAME        = 'check_geom';

sub print_help;
sub print_usage;
sub process_arguments;

$ENV{'PATH'}     = '@TRUSTED_PATH@';
$ENV{'BASH_ENV'} = '';
$ENV{'ENV'}      = '';

my $state        = 'UNKNOWN';
my $msg          = 'FAILURE';

if ( $^O ne 'freebsd' ) {
    print "This plugin is only applicable on FreeBSD.\n";
    exit $ERRORS{'WARNING'}
}

GetOptions (
    'V' => \$opt_V, 'version' => \$opt_V,
    'h' => \$opt_h, 'help' => \$opt_h,
    'c=s' => \$opt_class, 'class=s' => \$opt_class,
    'v=s' => \$opt_vol, 'volume=s' => \$opt_vol,
) or ( print_help() and exit $ERRORS{'OK'});

if ($opt_V) {
    print_revision($PROGNAME,'@NP_VERSION@');
    exit $ERRORS{'OK'};
}

if ($opt_h) {
    print_help();
    exit $ERRORS{'OK'};
}

my $class = $opt_class
    or ( print_usage() and exit $ERRORS{'WARNING'} );

my $volume = q();
my $regex  = '^\s*' . $class . '\/';

if ( $volume = $opt_V ) {
    $regex  = $regex . $volume . '\s';
}

my $found     = 0;
my $exist     = 0;
my $status    = q();
my $stateflag = 0;
my $name      = q();
my $compo     = q();
my $compoflag = 0;
my $output    = q();

sub endunit {

    $found  = 0;
    $output = "$output - $name $status { $compo }";

    if ( $class eq 'mirror' and $status !~ m/COMPLETE/ ) {
        if ( $compoflag >= 2 && $state ne 'CRITICAL' ) {
            $state = 'WARNING';
        }
        else {
            $state = 'CRITICAL';
        }
    }

    if ( $class eq 'raid3' and $status !~ m/COMPLETE/ ) {

        my $statcommand = "geom $class list $volume";
        my $unit        = 0;

        open my $STAT, '-|', $statcommand
          or ( print("$state $statcommand returns no result!")
            and exit $ERRORS{$state} );

        while ( my $line = <$STAT> ) {
            next unless ( $line =~ m/Components:/ );
            ($unit) = $line =~ m/([0-9]+)$/;
        }

        $state = 'WARNING'
            if ( $compoflag == $unit and $state ne 'CRITICAL' );

    }

    $compoflag = 0;

}

my $statcommand = "geom $class status";
open my $STAT, '-|', $statcommand
  or ( print "$state $statcommand returns no result!" and exit $ERRORS{$state} );

while ( my $line = <$STAT> ) {

    chomp $line;
    if ($found) {
        if ( $line =~ m/^\s*$class\// ) {
            &endunit;
        }
        else {
            my ($vgh) = $line =~ m/\s+(.*)/;
            $compo = "$compo , $vgh";
            $compoflag++;
        }
    }

    if ( $line =~ m/$regex/ ) {
        ( $name, $status, $compo ) = $line =~ m/^\s*(\S+)\s+(\S+)\s+(.*)$/;
        $found = 1;
        $exist++;
        $compoflag++;

        if ( ( $class eq 'mirror' or $class eq 'raid3' )
            and $status =~ m/COMPLETE/ )
        {
            $stateflag++;
        }

        if ( (
                   $class eq 'stripe'
                or $class eq 'concat'
                or $class eq 'shsec'
            )
            and $status =~ m/UP/
          )
        {
            $stateflag++;
        }
    }

}

if ( $found != 0 ) {
    &endunit;
}

close($STAT);

unless ($exist) {

    $state  = 'CRITICAL';
    $volume = 'volumes' unless $volume;
    $msg    = sprintf "%s %s unavailable, non-existant, or not responding!\n",
      $class, $volume;

    print $state, ' ', $msg;
    exit $ERRORS{$state}

}

$state = 'CRITICAL'
  if ( $state eq 'UNKNOWN' and $stateflag < $exist );

$state = 'OK'
    if ( $state eq 'UNKNOWN' );

#goats away!
my $perfdata = sprintf '%s=%d;;;0;', 'geom_' . $class, $found;
$msg = sprintf "%s/%s %s { %s }|%s\n", $class, $volume, $status, $compo,
    $perfdata;
print $state, ' ', $msg;
exit $ERRORS{$state};

sub print_usage () {
    print "Usage:\n";
    print "  $PROGNAME -c <class> [-v <volume>]\n";
    print "  $PROGNAME [-h | --help]\n";
    print "  $PROGNAME [-V | --version]\n";
}

sub print_help () {
    print_revision( $PROGNAME, '@NP_VERSION@' );
    print "Copyright (c) 2015 Dean Hamstead, Nathan Butcher\n\n";
    print_usage();
    print "\n";
    print "  -c | --class  :  Class of geom module (stripe, mirror, raid5 etc)\n";
    print "  -v | --volume :  Geom volume name\n";
    print "\n";
    support();
}
