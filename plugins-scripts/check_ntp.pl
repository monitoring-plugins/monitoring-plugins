#!/usr/bin/perl -w

# (c)1999 Ian Cass, Knowledge Matters Ltd.
# Read the GNU copyright stuff for all the legalese
#
# Check NTP time servers plugin. This plugin requires the ntpdate utility to
# be installed on the system, however since it's part of the ntp suite, you 
# should already have it installed.
#
# $Id$
# 
# Nothing clever done in this program - its a very simple bare basics hack to
# get the job done.
#
# Things to do...
# check @words[9] for time differences greater than +/- x secs & return a
# warning.
#
# (c) 1999 Mark Jewiss, Knowledge Matters Limited
# 22-9-1999, 12:45
#
# Modified script to accept 2 parameters or set defaults.
# Now issues warning or critical alert is time difference is greater than the 
# time passed.
#
# These changes have not been tested completely due to the unavailability of a
# server with the incorrect time.
#
# (c) 1999 Bo Kersey, VirCIO - Managed Server Solutions <bo@vircio.com>
# 22-10-99, 12:17
#
# Modified the script to give useage if no parameters are input.
#
# Modified the script to check for negative as well as positive 
# time differences.
#
# Modified the script to work with ntpdate 3-5.93e Wed Apr 14 20:23:03 EDT 1999
#
# Modified the script to work with ntpdate's that return adjust or offset...
#
#
# Script modified 2000 June 01 by William Pietri <william@bianca.com>
#
# Modified script to handle weird cases:
#     o NTP server doesn't respond (e.g., has died)
#     o Server has correct time but isn't suitable synchronization
#           source. This happens while starting up and if contact
#           with master has been lost.
#
# Modifed to run under Embedded Perl  (sghosh@users.sf.net)
#   - combined logic some blocks together..
# 
# Added ntpdate check for stratum 16 desynch peer (James Fidell) Feb 03, 2003
#


require 5.004;
use POSIX;
use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_H $opt_w $opt_c $verbose $PROGNAME);
use lib utils.pm ;
use utils qw($TIMEOUT %ERRORS &print_revision &support);

$PROGNAME="check_ntp";

sub print_help ();
sub print_usage ();

$ENV{'PATH'}='';
$ENV{'BASH_ENV'}='';
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V, "version"    => \$opt_V,
	 "h"   => \$opt_h, "help"       => \$opt_h,
	 "v" => \$verbose, "verbose"  => \$verbose,
	 "w=f" => \$opt_w, "warning=f"  => \$opt_w,   # offset|adjust warning if above this number
	 "c=f" => \$opt_c, "critical=f" => \$opt_c,   # offset|adjust critical if above this number
	 "H=s" => \$opt_H, "hostname=s" => \$opt_H);

if ($opt_V) {
	print_revision($PROGNAME,'$Revision$ ');
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

$opt_H = shift unless ($opt_H);
my $host = $1 if ($opt_H && $opt_H =~ m/^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|[a-zA-Z][-a-zA-Z0-9]+(\.[a-zA-Z][-a-zA-Z0-9]+)*)$/);
unless ($host) {
	print "No target host specified\n";
	print_usage();
	exit $ERRORS{'UNKNOWN'};
}

($opt_w) || ($opt_w = 60);
my $warning = $1 if ($opt_w =~ /([0-9.]+)/);

($opt_c) || ($opt_c = 120);
my $critical = $1 if ($opt_c =~ /([0-9.]+)/);


if ($critical < $warning ) {
	print "Critical offset should be larger than warning offset\n";
	print_usage();
	exit $ERRORS{"UNKNOWN"};
}

my $stratum = -1;
my $ignoreret = 0;
my $answer = undef;
my $offset = undef;
my $msg; # first line of output to print if format is invalid

my $state = $ERRORS{'UNKNOWN'};
my $ntpdate_error = $ERRORS{'UNKNOWN'};
my $dispersion_error = $ERRORS{'UNKNOWN'};

my $key = undef;
# some systems don't have a proper ntpdc/xntpdc
my $have_ntpdc = undef;
if ($utils::PATH_TO_NTPDC && -x $utils::PATH_TO_NTPDC ) {
	$have_ntpdc = 1;  
}else{
	$have_ntpdc = 0;
}

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
	print ("ERROR: No response from ntp server (alarm)\n");
	exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);


###
###
### First, check ntpdate
###
###

if (!open (NTPDATE, "$utils::PATH_TO_NTPDATE -q $host 2>&1 |")) {
	print "Could not open ntpdate\n";
	exit $ERRORS{"UNKNOWN"};
}

while (<NTPDATE>) {
	print if ($verbose);
	$msg = $_ unless ($msg);
	
	if (/stratum\s(\d+)/) {
		$stratum = $1;
	}
	
	if (/(offset|adjust)\s+([-.\d]+)/i) {
		$offset = $2;

		# An offset of 0.000000 with an error is probably bogus. Actually,
		# it's probably always bogus, but let's be paranoid here.
		if ($offset == 0) { undef $offset;}

		$ntpdate_error = defined ($offset) ? $ERRORS{"OK"} : $ERRORS{"CRITICAL"};
		print "ntperr = $ntpdate_error \n" if $verbose;
	
	}

	if (/no server suitable for synchronization found/) {
		if ($stratum == 16) {
			$ntpdate_error = $ERRORS{"WARNING"};
			$msg = "Desynchronized peer server found";
			$ignoreret=1;
		}
		else {
			$ntpdate_error = $ERRORS{"CRITICAL"};
			$msg = "No suitable peer server found - ";
		}
	}

}

close (NTPDATE); 
# declare an error if we also get a non-zero return code from ntpdate
# unless already set to critical
if ( $? && !$ignoreret ) {
	print "stderr = $? : $! \n" if $verbose;
	$ntpdate_error = $ntpdate_error == $ERRORS{"CRITICAL"} ? $ERRORS{"CRITICAL"} : $ERRORS{"UNKNOWN"}  ;
	print "ntperr = $ntpdate_error : $!\n" if $verbose;
}

###
###
### Then scan xntpdc/ntpdc if it exists
### and look in the 8th column for dispersion (ntpd v4) or jitter (ntpd v3)
###

if ($have_ntpdc) {

	if ( open(NTPDC,"$utils::PATH_TO_NTPDC -s $host 2>&1 |") ) {
		while (<NTPDC>) {
			print $_ if ($verbose);
			if (/([^\s]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)/) {
				if ($8 gt $critical) {
					print "Dispersion_crit = $8 :$critical\n" if ($verbose);
					$dispersion_error = $ERRORS{'CRITICAL'};
				} elsif ($8 gt $warning ) {
					print "Dispersion_warn = $8 :$warning \n" if ($verbose);
					$dispersion_error = $ERRORS{'WARNING'};
				} else {
					$dispersion_error = $ERRORS{'OK'};
				}
			}
		}
		close NTPDC;
	}
}


if ($ntpdate_error != $ERRORS{'OK'}) {
	$state = $ntpdate_error;
	if ($ntpdate_error == $ERRORS{'WARNING'} ) {
		$answer = $msg . "\n";
	}
	else {
		$answer = $msg . "Server for ntp probably down\n";
	}

	if (defined($offset) && abs($offset) > $critical) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Server Error and time difference $offset seconds greater than +/- $critical sec\n";
	} elsif (defined($offset) && abs($offset) > $warning) {
		$answer = "Server error and time difference $offset seconds greater than +/- $warning sec\n";
	}

} elsif ($have_ntpdc && $dispersion_error != $ERRORS{'OK'}) {
	$state = $dispersion_error;
	$answer = "Dispersion too high\n";
	if (defined($offset) && abs($offset) > $critical) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Dispersion error and time difference $offset seconds greater than +/- $critical sec\n";
	} elsif (defined($offset) && abs($offset) > $warning) {
		$answer = "Dispersion error and time difference $offset seconds greater than +/- $warning sec\n";
	}

} else { # no errors from ntpdate or xntpdc
	if (defined $offset) {
		if (abs($offset) > $critical) {
			$state = $ERRORS{'CRITICAL'};
			$answer = "Time difference $offset seconds greater than +/- $critical sec\n";
		} elsif (abs($offset) > $warning) {
			$state = $ERRORS{'WARNING'};
			$answer = "Time difference $offset seconds greater than +/- $warning sec\n";
		} elsif (abs($offset) <= $warning) {
			$state = $ERRORS{'OK'};
			$answer = "Time difference $offset seconds\n";
		}
	} else { # no offset defined
		$state = $ERRORS{'UNKNOWN'};
		$answer = "Invalid format returned from ntpdate ($msg)\n";
	}
}

foreach $key (keys %ERRORS) {
	if ($state==$ERRORS{$key}) {
		print ("$key: $answer");
		last;
	}
}
exit $state;

sub print_usage () {
	print "Usage: $PROGNAME -H <host> [-w <warn>] [-c <crit>] [-v verbose]\n";
}

sub print_help () {
	print_revision($PROGNAME,'$Revision$');
	print "Copyright (c) 2000 Bo Kersey/Karl DeBisschop\n";
	print "\n";
	print_usage();
	print "\n";
	print "<warn> = Clock offset in seconds at which a warning message will be generated.\n	Defaults to 60.\n";
	print "<crit> = Clock offset in seconds at which a critical message will be generated.\n	Defaults to 120.\n\n";
	print "The same warning and critical values are used to check against the dispersion \n";
	print "column of ntpdc/xntpdc for the host being queried.\n\n";
	support();
}
