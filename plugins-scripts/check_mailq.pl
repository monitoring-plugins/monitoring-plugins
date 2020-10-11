#!@PERL@ -w

# check_mailq - check to see how many messages are in the smtp queue awating
#   transmittal.  
#
# Initial version support sendmail's mailq command
#  Support for mutiple sendmail queues (Carlos Canau)
#  Support for qmail (Benjamin Schmid)

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
use vars qw($opt_V $opt_h $opt_v $verbose $PROGNAME $opt_w $opt_c $opt_t $opt_s
					$opt_M $mailq $status $state $msg $msg_q $msg_p $mailq @lines
					%srcdomains %dstdomains);
use FindBin;
use lib "$FindBin::Bin";
use utils qw(%ERRORS &print_revision &support &usage );

my ($sudo);

sub print_help ();
sub print_usage ();
sub process_arguments ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';
$PROGNAME = "check_mailq";
$mailq = 'sendmail';	# default
$msg_q = 0 ;
$msg_p = 0 ;
$state = $ERRORS{'UNKNOWN'};

Getopt::Long::Configure('bundling');
$status = process_arguments();
if ($status){
	print "ERROR: processing arguments\n";
	exit $ERRORS{"UNKNOWN"};
}

if ($opt_s) {
	if (defined $utils::PATH_TO_SUDO && -x $utils::PATH_TO_SUDO) {
		$sudo = $utils::PATH_TO_SUDO;
	} else {
		print "ERROR: Cannot execute sudo\n";
		exit $ERRORS{'UNKNOWN'};
	}
} else {
	$sudo = "";
}

$SIG{'ALRM'} = sub {
	print ("ERROR: timed out waiting for $utils::PATH_TO_MAILQ \n");
	exit $ERRORS{"WARNING"};
};
alarm($opt_t);

# switch based on MTA

if ($mailq eq "sendmail") {

	## open mailq 
	if ( defined $utils::PATH_TO_MAILQ && -x $utils::PATH_TO_MAILQ ) {
		if (! open (MAILQ, "$sudo $utils::PATH_TO_MAILQ | " ) ) {
			print "ERROR: could not open $utils::PATH_TO_MAILQ \n";
			exit $ERRORS{'UNKNOWN'};
		}
	}elsif( defined $utils::PATH_TO_MAILQ){
		unless (-x $utils::PATH_TO_MAILQ) {
			print "ERROR: $utils::PATH_TO_MAILQ is not executable by (uid $>:gid($)))\n";
			exit $ERRORS{'UNKNOWN'};
		}
	} else {
		print "ERROR: \$utils::PATH_TO_MAILQ is not defined\n";
		exit $ERRORS{'UNKNOWN'};
	}
#  single queue empty
##/var/spool/mqueue is empty
#  single queue: 1
##                /var/spool/mqueue (1 request)
##----Q-ID---- --Size-- -----Q-Time----- ------------Sender/Recipient------------
##h32E30p01763     2782 Wed Apr  2 15:03 <silvaATkpnqwest.pt>
##      8BITMIME
##                                       <silvaATeunet.pt>

#  multi queue empty
##/var/spool/mqueue/q0/df is empty
##/var/spool/mqueue/q1/df is empty
##/var/spool/mqueue/q2/df is empty
##/var/spool/mqueue/q3/df is empty
##/var/spool/mqueue/q4/df is empty
##/var/spool/mqueue/q5/df is empty
##/var/spool/mqueue/q6/df is empty
##/var/spool/mqueue/q7/df is empty
##/var/spool/mqueue/q8/df is empty
##/var/spool/mqueue/q9/df is empty
##/var/spool/mqueue/qA/df is empty
##/var/spool/mqueue/qB/df is empty
##/var/spool/mqueue/qC/df is empty
##/var/spool/mqueue/qD/df is empty
##/var/spool/mqueue/qE/df is empty
##/var/spool/mqueue/qF/df is empty
##                Total Requests: 0
#  multi queue: 1
##/var/spool/mqueue/q0/df is empty
##/var/spool/mqueue/q1/df is empty
##/var/spool/mqueue/q2/df is empty
##                /var/spool/mqueue/q3/df (1 request)
##----Q-ID---- --Size-- -----Q-Time----- ------------Sender/Recipient------------
##h32De2f23534*      48 Wed Apr  2 14:40 nocol
##                                       nouserATEUnet.pt
##                                       canau
##/var/spool/mqueue/q4/df is empty
##/var/spool/mqueue/q5/df is empty
##/var/spool/mqueue/q6/df is empty
##/var/spool/mqueue/q7/df is empty
##/var/spool/mqueue/q8/df is empty
##/var/spool/mqueue/q9/df is empty
##/var/spool/mqueue/qA/df is empty
##/var/spool/mqueue/qB/df is empty
##/var/spool/mqueue/qC/df is empty
##/var/spool/mqueue/qD/df is empty
##/var/spool/mqueue/qE/df is empty
##/var/spool/mqueue/qF/df is empty
##                Total Requests: 1

	
	while (<MAILQ>) {
	
		# match email addr on queue listing
		if ( (/<.*@.*\.(\w+\.\w+)>/) || (/<.*@(\w+\.\w+)>/) ) {
			my $domain = $1;
			if (/^\w+/) {
	  		print "$utils::PATH_TO_MAILQ = srcdomain = $domain \n" if $verbose ;
		    $srcdomains{$domain} ++;
			}
			next;
		}
	
		#
		# ...
		# sendmail considers a message with more than one destiny, say N, to the same MX 
		# to have N messages in queue.
		# we will only consider one in this code
		if (( /\s\(reply:\sread\serror\sfrom\s.*\.(\w+\.\w+)\.$/ ) || ( /\s\(reply:\sread\serror\sfrom\s(\w+\.\w+)\.$/ ) ||
			( /\s\(timeout\swriting\smessage\sto\s.*\.(\w+\.\w+)\.:/ ) || ( /\s\(timeout\swriting\smessage\sto\s(\w+\.\w+)\.:/ ) ||
			( /\s\(host\smap:\slookup\s\(.*\.(\w+\.\w+)\):/ ) || ( /\s\(host\smap:\slookup\s\((\w+\.\w+)\):/ ) || 
			( /\s\(Deferred:\s.*\s.*\.(\w+\.\w+)\.\)/ ) || ( /\s\(Deferred:\s.*\s(\w+\.\w+)\.\)/ ) ) {
	
			print "$utils::PATH_TO_MAILQ = dstdomain = $1 \n" if $verbose ;
			$dstdomains{$1} ++;
		}
	
		if (/\s+\(I\/O\serror\)/) {
			print "$utils::PATH_TO_MAILQ = dstdomain = UNKNOWN \n" if $verbose ;
			$dstdomains{'UNKNOWN'} ++;
		}

		# Finally look at the overall queue length
		#
		if (/mqueue/) {
			print "$utils::PATH_TO_MAILQ = $_ "if $verbose ;
			if (/ \((\d+) request/) {
	    	#
		    # single queue: first line
		    # multi queue: one for each queue. overwrite on multi queue below
	  	  $msg_q = $1 ;
			}
		} elsif (/^\s+Total\sRequests:\s(\d+)$/i) {
			print "$utils::PATH_TO_MAILQ = $_ \n" if $verbose ;
			#
			# multi queue: last line
			$msg_q = $1 ;
		}
	
	}
	

	## close mailq

	close (MAILQ); 

	if ( $? ) {
		print "CRITICAL: Error code ".($?>>8)." returned from $utils::PATH_TO_MAILQ",$/;
		exit $ERRORS{CRITICAL};
	}

	## shut off the alarm
	alarm(0);



	## now check the queue length(s)

	if ($msg_q == 0) {
		$msg = "OK: $mailq mailq is empty";
		$state = $ERRORS{'OK'};
	} else {
		print "msg_q = $msg_q warn=$opt_w crit=$opt_c\n" if $verbose;
	
		# overall queue length
		if ($msg_q < $opt_w) {
			$msg = "OK: $mailq mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
			$state = $ERRORS{'OK'};
		}elsif ($msg_q >= $opt_w  && $msg_q < $opt_c) {
			$msg = "WARNING: $mailq mailq is $msg_q (threshold w = $opt_w)";
			$state = $ERRORS{'WARNING'};
		}else {
			$msg = "CRITICAL: $mailq mailq is $msg_q (threshold c = $opt_c)";
			$state = $ERRORS{'CRITICAL'};
		}
	}

} # end of ($mailq eq "sendmail")
elsif ( $mailq eq "postfix" ) {

     ## open mailq
        if ( defined $utils::PATH_TO_MAILQ && -x $utils::PATH_TO_MAILQ ) {
                if (! open (MAILQ, "$sudo $utils::PATH_TO_MAILQ | " ) ) {
                        print "ERROR: could not open $utils::PATH_TO_MAILQ \n";
                        exit $ERRORS{'UNKNOWN'};
                }
        }elsif( defined $utils::PATH_TO_MAILQ){
                unless (-x $utils::PATH_TO_MAILQ) {
                        print "ERROR: $utils::PATH_TO_MAILQ is not executable by (uid $>:gid($)))\n";
                        exit $ERRORS{'UNKNOWN'};
                }
        } else {
                print "ERROR: \$utils::PATH_TO_MAILQ is not defined\n";
                exit $ERRORS{'UNKNOWN'};
        }


        @lines = reverse <MAILQ>;

        # close qmail-qstat
        close MAILQ;

        if ( $? ) {
		print "CRITICAL: Error code ".($?>>8)." returned from $utils::PATH_TO_MAILQ",$/;
		exit $ERRORS{CRITICAL};
        }

        ## shut off the alarm
        alarm(0);

        # check queue length
        if ($lines[0]=~/Kbytes in (\d+)/) {
                $msg_q = $1 ;
	}elsif ($lines[0]=~/Mail queue is empty/) {
		$msg_q = 0;
        }else{
                print "Couldn't match $utils::PATH_TO_MAILQ output\n";
                exit   $ERRORS{'UNKNOWN'};
        }

        # check messages not processed
        #if ($lines[1]=~/^messages in queue but not yet preprocessed: (\d+)/) {
        #        my $msg_p = $1;
        #}else{
        #        print "Couldn't match $utils::PATH_TO_MAILQ output\n";
        #        exit  $ERRORS{'UNKNOWN'};
        #}

        # check queue length(s)
        if ($msg_q == 0){
                $msg = "OK: $mailq mailq reports queue is empty";
                $state = $ERRORS{'OK'};
        } else {
                print "msg_q = $msg_q warn=$opt_w crit=$opt_c\n" if $verbose;

                # overall queue length
                if ($msg_q < $opt_w) {
                        $msg = "OK: $mailq mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
                        $state = $ERRORS{'OK'};
                }elsif  ($msg_q >= $opt_w  && $msg_q < $opt_c) {
                        $msg = "WARNING: $mailq mailq is $msg_q (threshold w = $opt_w)";
                        $state = $ERRORS{'WARNING'};
                }else {
                        $msg = "CRITICAL: $mailq mailq is $msg_q (threshold c = $opt_c)";
                        $state = $ERRORS{'CRITICAL'};
                }
        }
} # end of ($mailq eq "postfix")
elsif ( $mailq eq "qmail" ) {

	# open qmail-qstat 
	if ( defined $utils::PATH_TO_QMAIL_QSTAT && -x $utils::PATH_TO_QMAIL_QSTAT ) {
		if (! open (MAILQ, "$sudo $utils::PATH_TO_QMAIL_QSTAT | " ) ) {
			print "ERROR: could not open $utils::PATH_TO_QMAIL_QSTAT \n";
			exit $ERRORS{'UNKNOWN'};
		}
	}elsif( defined $utils::PATH_TO_QMAIL_QSTAT){
		unless (-x $utils::PATH_TO_QMAIL_QSTAT) {
			print "ERROR: $utils::PATH_TO_QMAIL_QSTAT is not executable by (uid $>:gid($)))\n";
			exit $ERRORS{'UNKNOWN'};
		}
	} else {
		print "ERROR: \$utils::PATH_TO_QMAIL_QSTAT is not defined\n";
		exit $ERRORS{'UNKNOWN'};
	}

	@lines = <MAILQ>;

	# close qmail-qstat
	close MAILQ;

	if ( $? ) {
		print "CRITICAL: Error code ".($?>>8)." returned from $utils::PATH_TO_MAILQ",$/;
		exit $ERRORS{CRITICAL};
	}

	## shut off the alarm
	alarm(0);

	# check queue length
	if ($lines[0]=~/^messages in queue: (\d+)/) {
		$msg_q = $1 ;
	}else{
		print "Couldn't match $utils::PATH_TO_QMAIL_QSTAT output\n";
		exit   $ERRORS{'UNKNOWN'};
	}

	# check messages not processed
	if ($lines[1]=~/^messages in queue but not yet preprocessed: (\d+)/) {
		my $msg_p = $1;
	}else{
		print "Couldn't match $utils::PATH_TO_QMAIL_QSTAT output\n";
		exit  $ERRORS{'UNKNOWN'};
	}


	# check queue length(s)
	if ($msg_q == 0){
		$msg = "OK: qmail-qstat reports queue is empty";
		$state = $ERRORS{'OK'};
	} else {
		print "msg_q = $msg_q warn=$opt_w crit=$opt_c\n" if $verbose;
		
		# overall queue length
		if ($msg_q < $opt_w) {
			$msg = "OK: $mailq mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
			$state = $ERRORS{'OK'};
		}elsif ($msg_q >= $opt_w  && $msg_q < $opt_c) {
			$msg = "WARNING: $mailq mailq is $msg_q (threshold w = $opt_w)";
			$state = $ERRORS{'WARNING'};
		}else {
			$msg = "CRITICAL: $mailq mailq is $msg_q (threshold c = $opt_c)";
			$state = $ERRORS{'CRITICAL'};
		}
	}				
		


} # end of ($mailq eq "qmail")
elsif ( $mailq eq "exim" ) {
	## open mailq 
	if ( defined $utils::PATH_TO_MAILQ && -x $utils::PATH_TO_MAILQ ) {
		if (! open (MAILQ, "$sudo $utils::PATH_TO_MAILQ | " ) ) {
			print "ERROR: could not open $utils::PATH_TO_MAILQ \n";
			exit $ERRORS{'UNKNOWN'};
		}
	}elsif( defined $utils::PATH_TO_MAILQ){
		unless (-x $utils::PATH_TO_MAILQ) {
			print "ERROR: $utils::PATH_TO_MAILQ is not executable by (uid $>:gid($)))\n";
			exit $ERRORS{'UNKNOWN'};
		}
	} else {
		print "ERROR: \$utils::PATH_TO_MAILQ is not defined\n";
		exit $ERRORS{'UNKNOWN'};
	}

	while (<MAILQ>) {
	    #22m  1.7K 19aEEr-0007hx-Dy <> *** frozen ***
            #root@exlixams.glups.fr

	    if (/\s[\w\d]{6}-[\w\d]{6}-[\w\d]{2}\s/) { # message id 19aEEr-0007hx-Dy
		$msg_q++ ;
	    }
	}
	close(MAILQ) ;

	if ( $? ) {
		print "CRITICAL: Error code ".($?>>8)." returned from $utils::PATH_TO_MAILQ",$/;
		exit $ERRORS{CRITICAL};
	}
	if ($msg_q < $opt_w) {
		$msg = "OK: $mailq mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
		$state = $ERRORS{'OK'};
	}elsif ($msg_q >= $opt_w  && $msg_q < $opt_c) {
		$msg = "WARNING: $mailq mailq is $msg_q (threshold w = $opt_w)";
		$state = $ERRORS{'WARNING'};
	}else {
		$msg = "CRITICAL: $mailq mailq is $msg_q (threshold c = $opt_c)";
		$state = $ERRORS{'CRITICAL'};
	}
} # end of ($mailq eq "exim")

elsif ( $mailq eq "nullmailer" ) {
	## open mailq
	if ( defined $utils::PATH_TO_MAILQ && -x $utils::PATH_TO_MAILQ ) {
		if (! open (MAILQ, "$sudo $utils::PATH_TO_MAILQ | " ) ) {
			print "ERROR: could not open $utils::PATH_TO_MAILQ \n";
			exit $ERRORS{'UNKNOWN'};
		}
	}elsif( defined $utils::PATH_TO_MAILQ){
		unless (-x $utils::PATH_TO_MAILQ) {
			print "ERROR: $utils::PATH_TO_MAILQ is not executable by (uid $>:gid($)))\n";
			exit $ERRORS{'UNKNOWN'};
		}
	} else {
		print "ERROR: \$utils::PATH_TO_MAILQ is not defined\n";
		exit $ERRORS{'UNKNOWN'};
	}

	while (<MAILQ>) {
	    #2006-06-22 16:00:00  282 bytes

	    if (/^[1-9][0-9]*-[01][0-9]-[0-3][0-9]\s[0-2][0-9]\:[0-2][0-9]\:[0-2][0-9]\s{2}[0-9]+\sbytes$/) {
		$msg_q++ ;
	    }
	}
	close(MAILQ) ;
	if ($msg_q < $opt_w) {
		$msg = "OK: $mailq mailq ($msg_q) is below threshold ($opt_w/$opt_c)";
		$state = $ERRORS{'OK'};
	}elsif ($msg_q >= $opt_w  && $msg_q < $opt_c) {
		$msg = "WARNING: $mailq mailq is $msg_q (threshold w = $opt_w)";
		$state = $ERRORS{'WARNING'};
	}else {
		$msg = "CRITICAL: $mailq mailq is $msg_q (threshold c = $opt_c)";
		$state = $ERRORS{'CRITICAL'};
	}
} # end of ($mailq eq "nullmailer")

# Perfdata support
print "$msg|unsent=$msg_q;$opt_w;$opt_c;0\n";
exit $state;


#####################################
#### subs


sub process_arguments(){
	GetOptions
		("V"   => \$opt_V, "version"	=> \$opt_V,
		 "v"   => \$opt_v, "verbose"	=> \$opt_v,
		 "h"   => \$opt_h, "help"		=> \$opt_h,
		 "M:s" => \$opt_M, "mailserver:s" => \$opt_M, # mailserver (default	sendmail)
		 "w=i" => \$opt_w, "warning=i"  => \$opt_w,   # warning if above this number
		 "c=i" => \$opt_c, "critical=i" => \$opt_c,	  # critical if above this number
		 "t=i" => \$opt_t, "timeout=i"  => \$opt_t,
		 "s"   => \$opt_s, "sudo"       => \$opt_s
		 );

	if ($opt_V) {
		print_revision($PROGNAME,'@NP_VERSION@');
		exit $ERRORS{'UNKNOWN'};
	}

	if ($opt_h) {
		print_help();
		exit $ERRORS{'UNKNOWN'};
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
		print "Warning (-w) cannot be greater than Critical (-c)!\n";
		exit $ERRORS{'UNKNOWN'};
	}

	if (defined $opt_M) {
		if ($opt_M =~ /^(sendmail|qmail|postfix|exim|nullmailer)$/) {
			$mailq = $opt_M ;
		}elsif( $opt_M eq ''){
			$mailq = 'sendmail';
		}else{
			print "-M: $opt_M is not supported\n";
			exit $ERRORS{'UNKNOWN'};
		}
	}else{
		if (defined $utils::PATH_TO_QMAIL_QSTAT
		    && -x $utils::PATH_TO_QMAIL_QSTAT)
		{
			$mailq = 'qmail';
		}
		elsif (-d '/var/lib/postfix' || -d '/var/local/lib/postfix'
		       || -e '/usr/sbin/postfix' || -e '/usr/local/sbin/postfix')
		{
			$mailq = 'postfix';
		}
		elsif (-d '/usr/lib/exim4' || -d '/usr/local/lib/exim4'
		       || -e '/usr/sbin/exim' || -e '/usr/local/sbin/exim')
		{
			$mailq = 'exim';
		}
		elsif (-d '/usr/lib/nullmailer' || -d '/usr/local/lib/nullmailer'
		       || -e '/usr/sbin/nullmailer-send'
		       || -e '/usr/local/sbin/nullmailer-send')
		{
			$mailq = 'nullmailer';
		}
		else {
			$mailq = 'sendmail';
		}
	}
		
	return $ERRORS{'OK'};
}

sub print_usage () {
	print "Usage: $PROGNAME -w <warn> -c <crit> [-M <MTA>] [-t <timeout>] [-s] [-v]\n";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2002 Subhendu Ghosh/Carlos Canau/Benjamin Schmid\n";
	print "\n";
	print_usage();
	print "\n";
	print "   Checks the number of messages in the mail queue (supports multiple sendmail queues, qmail)\n";
	print "   Feedback/patches to support non-sendmail mailqueue welcome\n\n";
	print "-w (--warning)   = Min. number of messages in queue to generate warning\n";
	print "-c (--critical)  = Min. number of messages in queue to generate critical alert ( w < c )\n";
	print "-t (--timeout)   = Plugin timeout in seconds (default = $utils::TIMEOUT)\n";
	print "-M (--mailserver) = [ sendmail | qmail | postfix | exim | nullmailer ] (default = autodetect)\n";
	print "-s (--sudo)      = Use sudo to call the mailq command\n";
	print "-h (--help)\n";
	print "-V (--version)\n";
	print "-v (--verbose)   = debugging output\n";
	print "\n\n";
	print "Note: -w and -c are required arguments.  -W and -C are optional.\n";
	print " -W and -C are applied to domains listed on the queues - both FROM and TO. (sendmail)\n";
	print " -W and -C are applied message not yet preproccessed. (qmail)\n";
	print " This plugin tries to autodetect which mailserver you are running,\n";
	print " you can override the autodetection with -M.\n";
	print " This plugin uses the system mailq command (sendmail) or qmail-stat (qmail)\n";
	print " to look at the queues. Mailq can usually only be accessed by root or \n";
	print " a TrustedUser. You will have to set appropriate permissions for the plugin to work.\n";
	print "";
	print "\n\n";
	support();
}
