#!/usr/bin/perl
#
#  check_nwstat.pl: Nagios plugin that uses Jim Drews' nwstat.pl for
#     MRTG instead of emulating it.  For use particularly with Cliff
#     Woolley's mrtgext.pl Unix companion to Drews' MRTGEXT.NLM, where
#     mrtgext.pl can contain custom commands that check_nwstat won't recognize,
#     though this also does its best to perfectly emulate the C version
#     of check_nwstat.
#


######################################################################
#  Configuration
######################################################################

$nwstatcmd = "/apps/mrtg/helpers/nwstat.pl";

use Getopt::Long;

$::host      = shift || &usage(%ERROR);
$::opt_v     = undef;
$::opt_wv    = undef;
$::opt_cv    = undef;
$::opt_to    = 10;
$::opt_url   = undef;

GetOptions (qw(v=s wv=i cv=i to=i url=s)) || &usage(%ERROR);

my $cmd1     = "";
my $cmd2     = "ZERO";
my $backward = 0;
my $desc     = "";
my $okstr    = "OK";
my $probstr  = "Problem";
my $result   = "";
my @CMD;
my %ERROR    = ("UNKNOWN"   => -1,
                "OK"        =>  0,
                "WARNING"   =>  1,
                "CRITICAL"  =>  2);
my $status   = $ERROR{"OK"};


######################################################################
#  Main program
######################################################################

$SIG{'ALRM'} = sub { 
    print "Connection timed out\n";
    exit $ERROR{"CRITICAL"};
};

# translate table for compatability with
# check_nwstat (C version)
SWITCH: for ($::opt_v) {
           /^LOAD(1|5|15)$/
                       && do { $desc = "Load <status> - Up <cmd2>, ".
                                       "$1-min load average = <cmd0>%";
                               $cmd1 = "UTIL$1";  last; };
           /^CONNS$/   && do { $desc = "Conns <status>: ".
                                       "<cmd0> current connections";
                               $cmd1 = "CONNECT"; last; };
           /^CDBUFF$/  && do { $desc = "Dirty cache buffers = <cmd0>";
                               $cmd1 = "S3";      last; };
           /^LTCH$/    && do { $desc = "Long term cache hits = <cmd0>%";
                               $cmd1 = "S1";
                               $backward = 1;     last; };
           /^CBUFF$/   && do { $desc = "Total cache buffers = <cmd0>";
                               $cmd1 = "S2";
                               $backward = 1;     last; };
           /^LRUM$/    && do { $desc = "LRU sitting time = <cmd0> minutes";
                               $cmd1 = "S5";
                               $backward = 1;     last; };
           /^VPF(.*)$/ && do { $desc = "<status><int(cmd0/1024)> MB ".
                                       "(<result>%) free on volume $1";
                               $okstr = ""; $probstr = "Only ";
                               $cmd1 = "VKF$1";
                               $cmd2 = "VKS$1";
                               $backward = 1;     last; };
           /^VKF/      && do { $desc = "<status><cmd0> KB free on volume $1";
                               $okstr = ""; $probstr = "Only ";
                               $cmd1 = "$::opt_v";
                               $backward = 1;     last; };
           /^$/        && die "Nothing to check!";
           $desc = "<status>: <cmd0>";
           $cmd1 = "$::opt_v";
        }


# begin timeout period, run the check
alarm($::opt_to);
open  ( CMD, "$nwstatcmd $host $cmd1 $cmd2|" ) || die "Couldn't execute nwstat";
@CMD = <CMD>;
close ( CMD );
alarm(0);

for (@CMD) { chomp; }

# for any variables that manipulate the results instead of
# just using <cmd0> directly, do that manipulation here into <result>
SWITCH: for ($::opt_v) {
           /^VPF/       && do { $result=int(("$CMD[0]"/"$CMD[1]")*100); last; };
           $result = "$CMD[0]";
        }

if ("$result" == -1) {
   $status = $ERROR{"UNKNOWN"};
   $desc = "Server returned \"variable unknown\"";
} elsif ("$result" == -2) {
   $status = $ERROR{"CRITICAL"};
   $desc = "Connection failed";
}

if (defined($::opt_cv) && $status == $ERROR{"OK"}) {
   if ($backward) {
      ("$result" <= "$::opt_cv") && ( $status = $ERROR{"CRITICAL"} );
   } else {
      ("$result" >= "$::opt_cv") && ( $status = $ERROR{"CRITICAL"} );
   }
}
if (defined($::opt_wv) && $status == $ERROR{"OK"}) {
   if ($backward) {
      ("$result" <= "$::opt_wv") && ( $status = $ERROR{"WARNING"} );
   } else {
      ("$result" >= "$::opt_wv") && ( $status = $ERROR{"WARNING"} );
   }
}

$desc =~ s/<status>/($status == $ERROR{"OK"})?"$okstr":"$probstr"/eg;
$desc =~ s/<([^>]*)cmd([0-3])([^>]*)>/eval("$1\"$CMD[$2]\"$3")/eg;
$desc =~ s/<result>/"$result"/eg;

if (defined($::opt_url)) {
   print "<A HREF=\"$::opt_url\">$desc</A>\n";
} else {
   print "$desc\n";
}
exit $status;


######################################################################
#  Subroutines
######################################################################

sub usage {

    %ERROR = shift;

    print <<EOF
check_nwstat.pl plugin for Nagios
by Cliff Woolley, (c) 2000

Usage: ./check_nwstat.pl <host_address> [-v variable] [-wv warn_value] [-cv crit_value] [-to to_sec] [-url url_value]

Options:
 [variable]   = Variable to check.  Valid variables include:
                  LOAD1    = 1 minute average CPU load
		  LOAD5	   = 5 minute average CPU load
		  LOAD15   = 15 minute average CPU load
                  CONNS    = number of currently licensed connections
                  VPF<vol> = percent free space on volume <vol>
	          VKF<vol> = KB of free space on volume <vol>
                  LTCH     = percent long term cache hits
                  CBUFF    = current number of cache buffers
		  CDBUFF   = current number of dirty cache buffers
		  LRUM     = LRU sitting time in minutes
 [warn_value] = Threshold for value necessary to result in a warning status
 [crit_value] = Threshold for value necessary to result in a critical status
 [to_sec]     = Number of secs before connection times out - default is 10 sec
 [url_value]  = URL to use in output as a hyperlink.  Useful to link to a page
                with more details or history for this variable (ie an MRTG page)

This plugin attempts to contact the MRTGEXT NLM running on a Novell server
to gather the requested system information.

Notes:
 - This plugin requres that the MRTGEXT.NLM file distributed with
   James Drews' MRTG extension for NetWare (available from
   http://www.engr.wisc.edu/~drews/mrtg/) be loaded on the Novell
   servers you wish to check.
 - Critical thresholds should be lower than warning thresholds when
   the following variables are checked: VPF, VKF, LTCH, CBUFF, and LRUM.
EOF
;

    exit $ERROR{"UNKNOWN"};
}

