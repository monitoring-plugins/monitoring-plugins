#!/usr/local/bin/perl -w

# check_mailq - check to see how many messages are in the smtp queue awating
#   transmittal.  
#
# Initial version support sendmail's mailq command

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
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
############################################################################

use POSIX;
use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_v $verbose $PROGNAME $opt_w $opt_c $opt_t $status $state $msg $msg_q );
use lib  utils.pm;
use utils qw(%ERRORS &print_revision &support &usage );

#my $MAILQ = "/usr/bin/mailq";   # need to migrate support to utils.pm and autoconf


sub print_help ();
sub print_usage ();
sub process_arguments ();

$ENV{'PATH'}='';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';
$PROGNAME = "check_mailq";

Getopt::Long::Configure('bundling');
$status = process_arguments();
if ($status){
	print "ERROR: processing arguments\n";
	exit $ERRORS{"UNKNOWN"};
}

$SIG{'ALRM'} = sub {
	print ("ERROR: timed out waiting for $utils::PATH_TO_MAILQ \n");
	exit $ERRORS{"WARNING"};
};
alarm($opt_t);

## open mailq 
if ( defined $utils::PATH_TO_MAILQ && -x $utils::PATH_TO_MAILQ ) {
	if (! open (MAILQ, "$utils::PATH_TO_MAILQ | " ) ) {
		print "ERROR: could not open $utils::PATH_TO_MAILQ \n";
		exit $ERRORS{'UNKNOWN'};
	}
}else{
	print "ERROR: Could not find mailq executable!\n";
	exit $ERRORS{'UNKNOWN'};
}

# only first line is relevant in this iteration.
while (<MAILQ>) {
	if (/mqueue/) {
		print "$utils::PATH_TO_MAILQ = $_ "if $verbose ;
		if (/empty/ ) {
			$msg = "OK: mailq is empty";
			$msg_q = 0;
			$state = $ERRORS{'OK'};
		}elsif ( /(\d+)/ ) {
			$msg_q = $1 ;

			print "msg_q = $msg_q warn=$opt_w crit=$opt_c\n" if $verbose;

			if ($msg_q < $opt_w) {
				$msg = "OK: mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
				$state = $ERRORS{'OK'};
			}elsif ($msg_q >= $opt_w  && $msg_q < $opt_c) {
				$msg = "WARNING: mailq is $msg_q (threshold w = $opt_w)";
				$state = $ERRORS{'WARNING'};
			}else {
				$msg = "CRITICAL: mailq is $msg_q (threshold c = $opt_c)";
				$state = $ERRORS{'CRITICAL'};
			}
			
		}

		last;
	}
	
}

close (MAILQ); 
# declare an error if we also get a non-zero return code from mailq
# unless already set to critical
if ( $? ) {
	print "stderr = $? : $! \n" if $verbose;
	$state = $state == $ERRORS{"CRITICAL"} ? $ERRORS{"CRITICAL"} : $ERRORS{"UNKNOWN"}  ;
	print "MAILQ error: $!\n" if $verbose;
}
## close mailq

# Perfdata support
print "$msg | mailq = $msg_q\n";
exit $state;


#####################################
#### subs


sub process_arguments(){
	GetOptions
		("V"   => \$opt_V, "version"	=> \$opt_V,
		 "v"   => \$opt_v, "verbose"	=> \$opt_v,
		 "h"   => \$opt_h, "help"		=> \$opt_h,
		 "w=i" => \$opt_w, "warning=i"  => \$opt_w,   # warning if above this number
		 "c=i" => \$opt_c, "critical=i" => \$opt_c,	  # critical if above this number
		 "t=i" => \$opt_t, "timeout=i"  => \$opt_t 
		 );

	if ($opt_V) {
		print_revision($PROGNAME,'$Revision$ ');
		exit $ERRORS{'OK'};
	}

	if ($opt_h) {
		print_help();
		exit $ERRORS{'OK'};
	}

	if (defined $opt_v ){
		$verbose = $opt_v;
	}

	unless (defined $opt_t) {
		$opt_t = $utils::TIMEOUT ;	# default timeout
	}

	unless (  defined $opt_w &&  defined $opt_c ) {
		print_usage();
		exit $ERRORS{'UNKNOWN'};
	}

	if ( $opt_w >= $opt_c) {
		print "Warning cannot be greater than Critical!\n";
		exit $ERRORS{'UNKNOWN'};
	}

	return $ERRORS{'OK'};
}

sub print_usage () {
	print "Usage: $PROGNAME [-w <warn>] [-c <crit>] [-t <timeout>] [-v verbose]\n";
}

sub print_help () {
	print_revision($PROGNAME,'$Revision$');
	print "Copyright (c) 2002 Subhendu Ghosh\n";
	print "\n";
	print_usage();
	print "\n";
	print "   Checks the number of messages in the mail queue\n";
	print "   Feedback/patches to support non-sendmail mailqueue welcome\n\n";
	print "-w (--warning)   = Min. number of messages in queue to generate warning\n";
	print "-c (--critical)  = Min. number of messages in queu to generate critical alert ( w < c )\n";
	print "-t (--timeout)   = Plugin timeout in seconds (default = $utils::TIMEOUT)\n";
	print "-h (--help)\n";
	print "-V (--version)\n";
	print "-v (--verbose)   = deebugging output\n";
	print "\n\n";
	support();
}
