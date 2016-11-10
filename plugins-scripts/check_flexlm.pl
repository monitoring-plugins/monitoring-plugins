#!@PERL@ -w
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
# Contrary to most other plugins, this script takes
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
#
# lmstat output patches from Steve Rigler/Cliff Rice 13-Apr-2002
# srigler@marathonoil.com,cerice@marathonoil.com



use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_F $opt_t $verbose $PROGNAME);
use FindBin;
use lib "$FindBin::Bin";
use utils qw(%ERRORS &print_revision &support &usage);

$PROGNAME="check_flexlm";

sub print_help ();
sub print_usage ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V,   "version"    => \$opt_V,
	 "h"   => \$opt_h,   "help"       => \$opt_h,
	 "v"   => \$verbose, "verbose"    => \$verbose,
	 "F=s" => \$opt_F,   "filename=s" => \$opt_F,
	 "t=i" => \$opt_t, "timeout=i"  => \$opt_t);

if ($opt_V) {
	print_revision($PROGNAME,'@NP_VERSION@');
	exit $ERRORS{'UNKNOWN'};
}

unless (defined $opt_t) {
	$opt_t = $utils::TIMEOUT ;	# default timeout
}


if ($opt_h) {print_help(); exit $ERRORS{'UNKNOWN'};}

unless (defined $opt_F) {
	print "Missing license.dat file\n";
	print_usage();
	exit $ERRORS{'UNKNOWN'};
}
# Just in case of problems, let's not hang the monitoring system
$SIG{'ALRM'} = sub {
	print "Timeout: No Answer from Client\n";
	exit $ERRORS{'UNKNOWN'};
};
alarm($opt_t);

my $lmstat = $utils::PATH_TO_LMSTAT ;
unless (-x $lmstat ) {
	print "Cannot find \"lmstat\"\n";
	exit $ERRORS{'UNKNOWN'};
}

($opt_F) || ($opt_F = shift) || usage("License file not specified\n");
my $licfile = $1 if ($opt_F =~ /^(.*)$/);
($licfile) || usage("Invalid filename: $opt_F\n");

print "$licfile\n" if $verbose;

if ( ! open(CMD,"$lmstat -c $licfile |") ) {
	print "ERROR: Could not open \"$lmstat -c $licfile\" ($!)\n";
	exit exit $ERRORS{'UNKNOWN'};
}

my $serverup = 0;
my @upsrv; 
my @downsrv;  # list of servers up and down

#my ($ls1,$ls2,$ls3,$lf1,$lf2,$lf3,$servers);
 
# key off of the term "license server" and 
# grab the status.  Keep going until "Vendor" is found
#

#
# Collect list of license servers by their status
# Vendor daemon status is ignored for the moment.

while ( <CMD> ) {
	next if (/^lmstat/);   # ignore 1st line - copyright
	next if (/^Flexible/); # ignore 2nd line - timestamp
	(/^Vendor/) && last;   # ignore Vendor daemon status
	print $_ if $verbose;
	
		if ($_ =~ /license server /) {	# matched 1 (of possibly 3) license server
			s/^\s*//;					#some servers start at col 1, other have whitespace
										# strip staring whitespace if any
			if ( $_ =~ /UP/) {
				$_ =~ /^(.*):/ ;
				push(@upsrv, $1);
				print "up:$1:\n" if $verbose;
			} else {
				$_ =~ /^(.*):/; 
				push(@downsrv, $1);
				print "down:$1:\n" if $verbose;
			}
		
		}
	

#	if ( /^License server status: [0-9]*@([-0-9a-zA-Z_]*),[0-9]*@([-0-9a-zA-Z_]*),[0-9]*@([-0-9a-zA-Z_]*)/ ) {
#	$ls1 = $1;
#	$ls2 = $2;
#	$ls3 = $3;
#	$lf1 = $lf2 = $lf3 = 0;
#	$servers = 3;
#  } elsif ( /^License server status: [0-9]*@([-0-9a-zA-Z_]*)/ ) {
#	$ls1 = $1;
#	$ls2 = $ls3 = "";
#	$lf1 = $lf2 = $lf3 = 0;
#	$servers = 1;
#  } elsif ( / *$ls1: license server UP/ ) {
#	print "$ls1 UP, ";
#	$lf1 = 1
#  } elsif ( / *$ls2: license server UP/ ) {
#	print "$ls2 UP, ";
#	$lf2 = 1
#  } elsif ( / *$ls3: license server UP/ ) {
#	print "$ls3 UP, ";
#	$lf3 = 1
#  } elsif ( / *([^:]*: UP .*)/ ) {
#	print " license server for $1\n";
#	$serverup = 1;
#  }

}

#if ( $serverup == 0 ) {
#    print " license server not running\n";
#    exit 2;	
#}

close CMD;

if ($verbose) {
	print "License Servers running: ".scalar(@upsrv) ."\n";
	foreach my $upserver (@upsrv) {
		print "$upserver\n";
	}
	print "License servers not running: ".scalar(@downsrv)."\n";
	foreach my $downserver (@downsrv) {
		print "$downserver\n";
	}
}

#
# print list of servers which are up. 
#
if (scalar(@upsrv) > 0) {
   print "License Servers running:";
   foreach my $upserver (@upsrv) {
      print "$upserver,";
   }
}
#
# Ditto for those which are down.
#
if (scalar(@downsrv) > 0) {
   print "License servers NOT running:";
   foreach my $downserver (@downsrv) {
      print "$downserver,";
   }
}

# perfdata
print "\n|flexlm::up:".scalar(@upsrv).";down:".scalar(@downsrv)."\n";

exit $ERRORS{'OK'} if ( scalar(@downsrv) == 0 );
exit $ERRORS{'WARNING'} if ( (scalar(@upsrv) > 0) && (scalar(@downsrv) > 0));

#exit $ERRORS{'OK'} if ( $servers == $lf1 + $lf2 + $lf3 );
#exit $ERRORS{'WARNING'} if ( $servers == 3 && $lf1 + $lf2 + $lf3 == 2 );
exit $ERRORS{'CRITICAL'};


sub print_usage () {
	print "Usage:
   $PROGNAME -F <filename> [-v] [-t] [-V] [-h]
   $PROGNAME --help
   $PROGNAME --version
";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2000 Ernst-Dieter Martin/Karl DeBisschop

Check available flexlm license managers

";
	print_usage();
	print "
-F, --filename=FILE
   Name of license file (usually \"license.dat\")
-v, --verbose
   Print some extra debugging information (not advised for normal operation)
-t, --timeout
   Plugin time out in seconds (default = $utils::TIMEOUT )
-V, --version
   Show version and license information
-h, --help
   Show this help screen

Flexlm license managers usually run as a single server or three servers and a
quorum is needed.  The plugin return OK if 1 (single) or 3 (triple) servers
are running, CRITICAL if 1(single) or 3 (triple) servers are down, and WARNING
if 1 or 2 of 3 servers are running\n
";
	support();
}
