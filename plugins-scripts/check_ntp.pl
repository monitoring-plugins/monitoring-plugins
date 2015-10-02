#!@PERL@ -w
#
# (c)1999 Ian Cass, Knowledge Matters Ltd.
# Read the GNU copyright stuff for all the legalese
#
# Check NTP time servers plugin. This plugin requires the ntpdate utility to
# be installed on the system, however since it's part of the ntp suite, you 
# should already have it installed.
#
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
# ntpdate - offset is in seconds
# changed ntpdc to ntpq - jitter/dispersion is in milliseconds
#
# Patch for for regex for stratum1 refid.

require 5.004;
use POSIX;
use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_H $opt_t $opt_w $opt_c $opt_O $opt_j $opt_k $verbose $PROGNAME $def_jitter $ipv4 $ipv6);
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);

$PROGNAME="check_ntp";

sub print_help ();
sub print_usage ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}='';
$ENV{'ENV'}='';

# defaults in sec
my $DEFAULT_OFFSET_WARN =  60;  # 1 minute
my $DEFAULT_OFFSET_CRIT = 120;  # 2 minutes
# default in millisec
my $DEFAULT_JITTER_WARN =   5000; # 5 sec
my $DEFAULT_JITTER_CRIT =  10000; # 10 sec

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V, "version"    => \$opt_V,
	 "h"   => \$opt_h, "help"       => \$opt_h,
	 "v"   => \$verbose, "verbose" 	=> \$verbose,
	 "4"   => \$ipv4, "use-ipv4"	=> \$ipv4,
	 "6"   => \$ipv6, "use-ipv6"	=> \$ipv6,
	 "w=f" => \$opt_w, "warning=f"  => \$opt_w,   # offset|adjust warning if above this number
	 "c=f" => \$opt_c, "critical=f" => \$opt_c,   # offset|adjust critical if above this number
	 "O"   => \$opt_O, "zero-offset" => \$opt_O,  # zero-offset  bad
	 "j=s" => \$opt_j, "jwarn=i"    => \$opt_j,   # jitter warning if above this number
	 "k=s" => \$opt_k, "jcrit=i"    => \$opt_k,   # jitter critical if above this number
	 "t=s" => \$opt_t, "timeout=i"  => \$opt_t,
	 "H=s" => \$opt_H, "hostname=s" => \$opt_H);

if ($opt_V) {
	print_revision($PROGNAME,'@NP_VERSION@');
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

# jitter test params specified
if (defined $opt_j || defined $opt_k ) {
	$def_jitter = 1;
}

$opt_H = shift unless ($opt_H);
my $host = $1 if ($opt_H && $opt_H =~ m/^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|[a-zA-Z][-a-zA-Z0-9]+(\.[a-zA-Z][-a-zA-Z0-9]+)*)$/);
unless ($host) {
	print "No target host specified\n";
	print_usage();
	exit $ERRORS{'UNKNOWN'};
}

my ($timeout, $owarn, $ocrit, $jwarn, $jcrit);

$timeout = $TIMEOUT;
($opt_t) && ($opt_t =~ /^([0-9]+)$/) && ($timeout = $1);

$owarn = $DEFAULT_OFFSET_WARN;
($opt_w) && ($opt_w =~ /^([0-9.]+)$/) && ($owarn = $1);

$ocrit = $DEFAULT_OFFSET_CRIT;
($opt_c) && ($opt_c =~ /^([0-9.]+)$/) && ($ocrit = $1);

$jwarn = $DEFAULT_JITTER_WARN;
($opt_j) && ($opt_j =~ /^([0-9]+)$/) && ($jwarn = $1);

$jcrit = $DEFAULT_JITTER_CRIT;
($opt_k) && ($opt_k =~ /^([0-9]+)$/) && ($jcrit = $1);

if ($ocrit < $owarn ) {
	print "Critical offset should be larger than warning offset\n";
	print_usage();
	exit $ERRORS{"UNKNOWN"};
}

if ($def_jitter) {
	if ($opt_k < $opt_j) {
		print "Critical jitter should be larger than warning jitter\n";
		print_usage();
		exit $ERRORS{'UNKNOWN'};
	}
}


my $stratum = -1;
my $ignoreret = 0;
my $answer = undef;
my $offset = undef;
my $jitter = undef;
my $syspeer = undef;
my $candidate = 0;
my @candidates;
my $msg; # first line of output to print if format is invalid

my $state = $ERRORS{'UNKNOWN'};
my $ntpdate_error = $ERRORS{'UNKNOWN'};
my $jitter_error = $ERRORS{'UNKNOWN'};

# some systems don't have a proper ntpq  (migrated from ntpdc)
my $have_ntpq = undef;
if ($utils::PATH_TO_NTPQ && -x $utils::PATH_TO_NTPQ ) {
	$have_ntpq = 1;  
}else{
	$have_ntpq = 0;
}

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
	print ("ERROR: No response from ntp server (alarm)\n");
	exit $ERRORS{"UNKNOWN"};
};
alarm($timeout);

# Determine protocol to be used for ntpdate and ntpq
my $ntpdate = $utils::PATH_TO_NTPDATE;
my $ntpq    = $utils::PATH_TO_NTPQ;
if ($ipv4) {
        $ntpdate .= " -4";
        $ntpq .= " -4";
}
elsif ($ipv6) {
        $ntpdate .= " -6";
        $ntpq .= " -6";
}
# else don't use any flags

###
###
### First, check ntpdate
###
###

if (!open (NTPDATE, $ntpdate . " -q $host 2>&1 |")) {
	print "Could not open $ntpdate: $!\n";
	exit $ERRORS{"UNKNOWN"};
}

my $out;
while (<NTPDATE>) {
	#print if ($verbose);  # noop
	$msg = $_ unless ($msg);
	$out .= "$_ ";
	
	if (/stratum\s(\d+)/) {
		$stratum = $1;
	}
	
	if (/(offset|adjust)\s+([-.\d]+)/i) {
		$offset = $2;

		# An offset of 0.000000 with an error is probably bogus. Actually,
		# it's probably always bogus, but let's be paranoid here.
		# Has been reported that 0.0000 happens in a production environment
		# on Solaris 8 so this check should be taken out - SF tracker 1150777
		if (defined $opt_O ) {
			if ($offset == 0) { undef $offset;}
		}

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
$out =~ s/\n//g;
close (NTPDATE) || 
    die $! ? "$out - Error closing $ntpdate pipe: $!"
           : "$out - Exit status: $? from $ntpdate\n";

# declare an error if we also get a non-zero return code from ntpdate
# unless already set to critical
if ( $? && !$ignoreret ) {
	print "stderr = $? : $! \n" if $verbose;
	$ntpdate_error = $ntpdate_error == $ERRORS{"CRITICAL"} ? $ERRORS{"CRITICAL"} : $ERRORS{"UNKNOWN"}  ;
	print "ntperr = $ntpdate_error : $!\n" if $verbose;
}

###
###
### Then scan xntpq/ntpq if it exists
### and look in the 11th column for jitter 
###
# Field 1: Tally Code ( Space, 'x','.','-','+','#','*','o')
#           Only match for '*' which implies sys.peer 
#           or 'o' which implies pps.peer
#           If both exist, the last one is picked. 
# Field 2: address of the remote peer
# Field 3: Refid of the clock (0.0.0.0 if unknown, WWWV/PPS/GPS/ACTS/USNO/PCS/... if Stratum1)
# Field 4: stratum (0-15)
# Field 5: Type of the peer: local (l), unicast (u), multicast (m) 
#          broadcast (b); not sure about multicast/broadcast
# Field 6: last packet receive (in seconds)
# Field 7: polling interval
# Field 8: reachability resgister (octal) 
# Field 9: delay
# Field 10: offset
# Field 11: dispersion/jitter
# 
# According to bug 773588 Some solaris xntpd implementations seemto match on
# "#" even though the docs say it exceeds maximum distance. Providing patch
# here which will generate a warining.

if ($have_ntpq) {

	if ( open(NTPQ, $ntpq . " -np $host 2>&1 |") ) {
		while (<NTPQ>) {
			print $_ if ($verbose);
			if ( /timed out/ ){
				$have_ntpq = 0 ;
				last ;
			}
			# number of candidates on <host> for sys.peer
			if (/^(\*|\+|\#|o])/) {
				++$candidate;
				push (@candidates, $_);
				print "Candidate count= $candidate\n" if ($verbose);
			}
			
			# match sys.peer or pps.peer
			if (/^(\*|o)(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)/) {
				$syspeer = $2;
				$stratum = $4;
				$jitter = $11;
				print "match $_ \n" if $verbose;
				if ($jitter > $jcrit) {
					print "Jitter_crit = $11 :$jcrit\n" if ($verbose);
					$jitter_error = $ERRORS{'CRITICAL'};
				} elsif ($jitter > $jwarn ) {
					print "Jitter_warn = $11 :$jwarn\n" if ($verbose);
					$jitter_error = $ERRORS{'WARNING'};
				} else {
					$jitter_error = $ERRORS{'OK'};
				}
			} else {
				print "No match!\n" if $verbose;
				$jitter = '(not parsed)';
			}
			
		}
		close NTPQ ||
            die $! ? "Error closing $ntpq pipe: $!"
                   : "Exit status: $? from $ntpq\n";

		# if we did not match sys.peer or pps.peer but matched # candidates only
		# generate a warning 
		# based on bug id 773588
		unless (defined $syspeer) {
			if ($#candidates >=0) {
				foreach my $c (@candidates) {
					$c =~ /^(#)([-0-9.\s]+)\s+([-0-9A-Za-z_().]+)\s+([-0-9.]+)\s+([lumb-]+)\s+([-0-9m.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)\s+([-0-9.]+)/;
					$syspeer = $2;
					$stratum = $4;
					$jitter = $11;
					print "candidate match $c \n" if $verbose;
					if ($jitter > $jcrit) {
						print "Candidate match - Jitter_crit = $11 :$jcrit\n" if ($verbose);
						$jitter_error = $ERRORS{'CRITICAL'};
					}elsif ($jitter > $jwarn ) {
						print "Candidate match - Jitter_warn = $11 :$jwarn \n" if ($verbose);
						$jitter_error = $ERRORS{'WARNING'};
					} else {
						$jitter_error = $ERRORS{'WARNING'};
					}
				}

			}
		}
	}
}


if ($ntpdate_error != $ERRORS{'OK'}) {
	$state = $ntpdate_error;
	if ($ntpdate_error == $ERRORS{'WARNING'} ) {
		$answer = $msg;
	}
	else {
		$answer = $msg . "Server for ntp probably down";
	}

	if (defined($offset) && abs($offset) > $ocrit) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Server Error and offset $offset sec > +/- $ocrit sec";
	} elsif (defined($offset) && abs($offset) > $owarn) {
		$answer = "Server error and offset $offset sec > +/- $owarn sec";
	} elsif (defined($jitter) && abs($jitter) > $jcrit) {
		$answer = "Server error and jitter $jitter msec > +/- $jcrit msec";
	} elsif (defined($jitter) && abs($jitter) > $jwarn) {
		$answer = "Server error and jitter $jitter msec > +/- $jwarn msec";
	}

} elsif ($have_ntpq && $jitter_error != $ERRORS{'OK'}) {
	$state = $jitter_error;
	$answer = "Jitter $jitter too high";
	if (defined($offset) && abs($offset) > $ocrit) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Jitter error and offset $offset sec > +/- $ocrit sec";
	} elsif (defined($offset) && abs($offset) > $owarn) {
		$answer = "Jitter error and offset $offset sec > +/- $owarn sec";
	} elsif (defined($jitter) && abs($jitter) > $jcrit) {
		$answer = "Jitter error and jitter $jitter msec > +/- $jcrit msec";
	} elsif (defined($jitter) && abs($jitter) > $jwarn) {
		$answer = "Jitter error and jitter $jitter msec > +/- $jwarn msec";
	}

} elsif( !$have_ntpq ) { # no errors from ntpdate and no ntpq or ntpq timed out
	if (abs($offset) > $ocrit) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Offset $offset sec > +/- $ocrit sec";
	} elsif (abs($offset) > $owarn) {
		$state = $ERRORS{'WARNING'};
		$answer = "Offset $offset sec > +/- $owarn sec";
	} elsif (( abs($offset) > $owarn) && $def_jitter ) {
		$state = $ERRORS{'WARNING'};
		$answer = "Offset $offset sec > +/- $owarn sec, ntpq timed out";
	} elsif ( $def_jitter ) {
		$state = $ERRORS{'WARNING'};
		$answer = "Offset $offset secs, ntpq timed out";
	} else{
		$state = $ERRORS{'OK'};
		$answer = "Offset $offset secs";
	}



} else { # no errors from ntpdate or ntpq
	if (abs($offset) > $ocrit) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Offset $offset sec > +/- $ocrit sec, jitter $jitter msec";
	} elsif (abs($jitter) > $jcrit ) {
		$state = $ERRORS{'CRITICAL'};
		$answer = "Jitter $jitter msec> +/- $jcrit msec, offset $offset sec";
	} elsif (abs($offset) > $owarn) {
		$state = $ERRORS{'WARNING'};
		$answer = "Offset $offset sec > +/- $owarn sec, jitter $jitter msec";
	} elsif (abs($jitter) > $jwarn ) {
		$state = $ERRORS{'WARNING'};
		$answer = "Jitter $jitter msec> +/- $jwarn msec, offset $offset sec";

	} else {
		$state = $ERRORS{'OK'};
		$answer = "Offset $offset secs, jitter $jitter msec, peer is stratum $stratum";
	}
	
}

foreach my $key (keys %ERRORS) {
	if ($state==$ERRORS{$key}) {
#		print ("NTP $key: $answer");
		print ("NTP $key: $answer|offset=$offset, jitter=" . $jitter/1000 .	",peer_stratum=$stratum\n");
		last;
	}
}
exit $state;


####
#### subs

sub print_usage () {
	print "Usage: $PROGNAME -H <host> [-46] [-O] [-w <warn>] [-c <crit>] [-j <warn>] [-k <crit>] [-v verbose]\n";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2003 Bo Kersey/Karl DeBisschop\n";
	print "\n";
	print_usage();
	print "
Checks the local timestamp offset versus <host> with ntpdate
Checks the jitter/dispersion of clock signal between <host> and its sys.peer with ntpq\n
-O (--zero-offset)
     A zero offset on \"ntpdate\" will generate a CRITICAL.\n
-w (--warning)
     Clock offset in seconds at which a warning message will be generated.\n	Defaults to $DEFAULT_OFFSET_WARN.
-c (--critical) 
     Clock offset in seconds at which a critical message will be generated.\n	Defaults to $DEFAULT_OFFSET_CRIT.
-j (--jwarn)
     Clock jitter in milliseconds at which a warning message will be generated.\n	Defaults to $DEFAULT_JITTER_WARN.
-k (--jcrit)
    Clock jitter in milliseconds at which a critical message will be generated.\n	Defaults to $DEFAULT_JITTER_CRIT.
    
    If jitter/dispersion is specified with -j or -k and ntpq times out, then a
    warning is returned.\n
-4 (--use-ipv4)
    Use IPv4 connection
-6 (--use-ipv6)
    Use IPv6 connection
\n";	
support();
}
