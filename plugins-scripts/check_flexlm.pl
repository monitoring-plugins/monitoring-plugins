#!/usr/bin/perl -w
#
# usage: 
#    check_flexlm.pl license_file
#
# Check available flexlm license managers.
# Use lmstat to check the status of the license server
# described by the license file given as argument.
# Check and interpret the output of lmstat
# and create returncodes and output.
#
# Contrary to the nagios concept, this script takes
# a file, not a hostname as an argument and returns
# the status of hosts and services described in that
# file. Use these hosts.cfg entries as an example
#
#host[anchor]=any host will do;some.address.com;;check-host-alive;3;120;24x7;1;1;1;
#service[anchor]=yodel;24x7;3;5;5;unix-admin;60;24x7;1;1;1;;check_flexlm!/opt/lic/licfiles/yodel_lic
#service[anchor]=yeehaw;24x7;3;5;5;unix-admin;60;24x7;1;1;1;;check_flexlm!/opt/lic/licfiles/yeehaw_lic
#command[check_flexlm]=/some/path/libexec/check_flexlm.pl $ARG1$
#
# Notes:
# - you need the lmstat utility which comes with flexlm.
# - set the correct path in the variable $lmstat.
#
# initial version: 9-10-99 Ernst-Dieter Martin edmt@infineon.com
#
# License: GPL
# $Id$
#


use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_F $verbose $PROGNAME);
use lib utils.pm;
use utils qw($TIMEOUT %ERRORS &print_revision &support &usage);

$PROGNAME="check_flexlm";

sub print_help ();
sub print_usage ();

$ENV{'PATH'}='';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V,   "version"    => \$opt_V,
	 "h"   => \$opt_h,   "help"       => \$opt_h,
	 "v"   => \$verbose, "verbose"    => \$verbose,
	 "F=s" => \$opt_F,   "filename=s" => \$opt_F);

if ($opt_V) {
	print_revision($PROGNAME,'$Revision$');
	exit $ERRORS{'OK'};
}

if ($opt_h) {print_help(); exit $ERRORS{'OK'};}

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
	print "No Answer from Client\n";
	exit 2;
};
alarm($TIMEOUT);

my $lmstat = $utils::PATH_TO_LMSTAT ;
unless (-x $lmstat ) {
	print "Cannot find \"lmstat\"\n";
	exit $ERRORS{'UNKNOWN'};
}

($opt_F) || ($opt_F = shift) || usage("License file not specified\n");
my $licfile = $1 if ($opt_F =~ /^(.*)$/);
($licfile) || usage("Invalid filename: $opt_F\n");

print "$licfile\n" if $verbose;

open CMD,"$lmstat -c $licfile |";

my $serverup = 0;
my ($ls1,$ls2,$ls3,$lf1,$lf2,$lf3,$servers);

while ( <CMD> ) {
  if ( /^License server status: [0-9]*@([-0-9a-zA-Z_]*),[0-9]*@([-0-9a-zA-Z_]*),[0-9]*@([-0-9a-zA-Z_]*)/ ) {
	$ls1 = $1;
	$ls2 = $2;
	$ls3 = $3;
	$lf1 = $lf2 = $lf3 = 0;
	$servers = 3;
  } elsif ( /^License server status: [0-9]*@([-0-9a-zA-Z_]*)/ ) {
	$ls1 = $1;
	$ls2 = $ls3 = "";
	$lf1 = $lf2 = $lf3 = 0;
	$servers = 1;
  } elsif ( / *$ls1: license server UP/ ) {
	print "$ls1 UP, ";
	$lf1 = 1
  } elsif ( / *$ls2: license server UP/ ) {
	print "$ls2 UP, ";
	$lf2 = 1
  } elsif ( / *$ls3: license server UP/ ) {
	print "$ls3 UP, ";
	$lf3 = 1
  } elsif ( / *([^:]*: UP .*)/ ) {
	print " license server for $1\n";
	$serverup = 1;
  }
}
if ( $serverup == 0 ) {
    print " license server not running\n";
    exit 2;	
}

exit $ERRORS{'OK'} if ( $servers == $lf1 + $lf2 + $lf3 );
exit $ERRORS{'WARNING'} if ( $servers == 3 && $lf1 + $lf2 + $lf3 == 2 );
exit $ERRORS{'CRITICAL'};


sub print_usage () {
	print "Usage:
   $PROGNAME -F <filename> [--verbose]
   $PROGNAME --help
   $PROGNAME --version
";
}

sub print_help () {
	print_revision($PROGNAME,'$Revision$');
	print "Copyright (c) 2000 Ernst-Dieter Martin/Karl DeBisschop

Check available flexlm license managers

";
	print_usage();
	print "
-F, --filename=FILE
   Name of license file
-v, --verbose
   Print some extra debugging information (not advised for normal operation)
-V, --version
   Show version and license information
-h, --help
   Show this help screen

";
	support();
}
