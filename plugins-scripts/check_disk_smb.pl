#!/usr/bin/perl -w
#
#
# check_disk.pl <host> <share> <user> <pass> [warn] [critical] [port]
#
# Nagios host script to get the disk usage from a SMB share
#
# Changes and Modifications
# =========================
# 7-Aug-1999 - Michael Anthon
#  Created from check_disk.pl script provided with netsaint_statd (basically
#  cause I was too lazy (or is that smart?) to write it from scratch)
# 8-Aug-1999 - Michael Anthon
#  Modified [warn] and [critical] parameters to accept format of nnn[M|G] to
#  allow setting of limits in MBytes or GBytes.  Percentage settings for large
#  drives is a pain in the butt
# 2-May-2002 - SGhosh fix for embedded perl
#
#

require 5.004;
use POSIX;
use strict;
use Getopt::Long;
use vars qw($opt_P $opt_V $opt_h $opt_H $opt_s $opt_W $opt_u $opt_p $opt_w $opt_c $opt_a $verbose);
use vars qw($PROGNAME);
use lib utils.pm ;
use utils qw($TIMEOUT %ERRORS &print_revision &support &usage);

sub print_help ();
sub print_usage ();

$PROGNAME = "check_disk_smb";

$ENV{'PATH'}='';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("v"   => \$verbose, "verbose"    => \$verbose,
	 "P=s" => \$opt_P, "port=s"     => \$opt_P,
	 "V"   => \$opt_V, "version"    => \$opt_V,
	 "h"   => \$opt_h, "help"       => \$opt_h,
	 "w=s" => \$opt_w, "warning=s"  => \$opt_w,
	 "c=s" => \$opt_c, "critical=s" => \$opt_c,
	 "p=s" => \$opt_p, "password=s" => \$opt_p,
	 "u=s" => \$opt_u, "username=s" => \$opt_u,
	 "s=s" => \$opt_s, "share=s"    => \$opt_s,
	 "W=s" => \$opt_W, "workgroup=s" => \$opt_W,
	 "H=s" => \$opt_H, "hostname=s" => \$opt_H,
	 "a=s" => \$opt_a, "address=s" => \$opt_a);

if ($opt_V) {
	print_revision($PROGNAME,'@NP_VERSION@'); #'
	exit $ERRORS{'OK'};
}

if ($opt_h) {print_help(); exit $ERRORS{'OK'};}

my $smbclient = $utils::PATH_TO_SMBCLIENT;
$smbclient    || usage("check requires smbclient, smbclient not set\n");
-x $smbclient || usage("check requires smbclient, $smbclient: $!\n");

# Options checking

($opt_H) || ($opt_H = shift @ARGV) || usage("Host name not specified\n");
my $host = $1 if ($opt_H =~ /^([-_.A-Za-z0-9 ]+\$?)$/);
($host) || usage("Invalid host: $opt_H\n");

($opt_s) || ($opt_s = shift @ARGV) || usage("Share volume not specified\n");
my $share = $1 if ($opt_s =~ /^([-_.A-Za-z0-9 ]+\$?)$/);
($share) || usage("Invalid share: $opt_s\n");

defined($opt_u) || ($opt_u = shift @ARGV) || ($opt_u = "guest");
my $user = $1 if ($opt_u =~ /^([-_.A-Za-z0-9\\]*)$/);
defined($user) || usage("Invalid user: $opt_u\n");

defined($opt_p) || ($opt_p = shift @ARGV) || ($opt_p = "");
my $pass = $1 if ($opt_p =~ /(.*)/);

($opt_w) || ($opt_w = shift @ARGV) || ($opt_w = 85);
my $warn = $1 if ($opt_w =~ /^([0-9]{1,2}\%?|100\%?|[0-9]+[kMG])$/);
($warn) || usage("Invalid warning threshold: $opt_w\n");

($opt_c) || ($opt_c = shift @ARGV) || ($opt_c = 95);
my $crit = $1 if ($opt_c =~ /^([0-9]{1,2}\%?|100\%?|[0-9]+[kMG])$/);
($crit) || usage("Invalid critical threshold: $opt_c\n");

# Execute the given command line and return anything it writes to STDOUT and/or
# STDERR.  (This might be useful for other plugins, too, so it should possibly
# be moved to utils.pm.)
sub output_and_error_of {
	local *CMD;
	local $/ = undef;
	my $pid = open CMD, "-|";
	if (defined($pid)) {
		if ($pid) {
			return <CMD>;
		} else {
			open STDERR, ">&STDOUT" and exec @_;
			exit(1);
		}
	}
	return undef;
}

# split the type from the unit value
#Check $warn and $crit for type (%/M/G) and set up for tests
#P = Percent, K = KBytes
my $warn_type;
my $crit_type;

if ($opt_w =~ /^([0-9]+)\%?$/) {
	$warn = "$1";
	$warn_type = "P";
} elsif ($opt_w =~ /^([0-9]+)k$/) {
	$warn_type = "K";
	$warn = $1;
} elsif ($opt_w =~ /^([0-9]+)M$/) {
	$warn_type = "K";
	$warn = $1 * 1024;
} elsif ($opt_w =~ /^([0-9]+)G$/) {
	$warn_type = "K";
	$warn = $1 * 1048576;
}
if ($opt_c =~ /^([0-9]+)\%?$/) {
	$crit = "$1";
	$crit_type = "P";
} elsif ($opt_c =~ /^([0-9]+)k$/) {
	$crit_type = "K";
	$crit = $1;
} elsif ($opt_c =~ /^([0-9]+)M$/) {
	$crit_type = "K";
	$crit = $1 * 1024;
} elsif ($opt_c =~ /^([0-9]+)G$/) {
	$crit_type = "K";
	$crit = $1 * 1048576;
}

# check if both warning and critical are percentage or size
unless( ( $warn_type eq "P" && $crit_type eq "P" ) || ( $warn_type ne "P" && $crit_type ne "P" ) ){
	$opt_w =~ s/\%/\%\%/g;
	$opt_c =~ s/\%/\%\%/g;
	usage("Both warning and critical should be same type- warning: $opt_w critical: $opt_c \n");
}

# verify warning is less than critical
if ( $warn_type eq "K") {
	unless ( $warn > $crit) {
		usage("Disk size: warning ($opt_w) should be greater than critical ($opt_c) \n");
	}
}else{
	unless ( $warn < $crit) {
		$opt_w =~ s/\%/\%\%/g;
		$opt_c =~ s/\%/\%\%/g;
		usage("Percentage: warning ($opt_w) should be less than critical ($opt_c) \n");
	}
}

my $workgroup = $1 if (defined($opt_W) && $opt_W =~ /(.*)/);

my $address = $1 if (defined($opt_a) && $opt_a =~ /(.*)/);

# end of options checking


my $state = "OK";
my $answer = undef;
my $res = undef;
my $perfdata = "";
my @lines = undef;

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub { 
	print "No Answer from Client\n";
	exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

# Execute a "du" on the share using smbclient program
# get the results into $res
my @cmd = (
	$smbclient,
	"//$host/$share",
	"-U", "$user%$pass",
	defined($workgroup) ? ("-W", $workgroup) : (),
	defined($address) ? ("-I", $address) : (),
	defined($opt_P) ? ("-p", $opt_P) : (),
	"-c", "du"
);

print join(" ", @cmd) . "\n" if ($verbose);
$res = output_and_error_of(@cmd) or exit $ERRORS{"UNKNOWN"};

#Turn off alarm
alarm(0);

#Split $res into an array of lines
@lines = split /\n/, $res;

#Get the last line into $_
$_ = $lines[$#lines-1];
#print "$_\n";

#Process the last line to get free space.  
#If line does not match required regexp, return an UNKNOWN error
if (/\s*(\d*) blocks of size (\d*)\. (\d*) blocks available/) {

	my ($avail_bytes) = $3 * $2;
	my ($total_bytes) = $1 * $2;
	my ($occupied_bytes) = $1 * $2 - $avail_bytes;
	my ($avail) = $avail_bytes/1024;
	my ($capper) = int(($3/$1)*100);
	my ($mountpt) = "\\\\$host\\$share";

	# TODO : why is the kB the standard unit for args ?
	my ($warn_bytes) = $total_bytes - $warn * 1024;
	if ($warn_type eq "P") {
		$warn_bytes = $warn * $1 * $2 / 100;
	}
	my ($crit_bytes) = $total_bytes - $crit * 1024;
	if ($crit_type eq "P") {
		$crit_bytes = $crit * $1 * $2 / 100;
	}


	if (int($avail / 1024) > 0) {
		$avail = int($avail / 1024);
		if (int($avail /1024) > 0) {
			$avail = (int(($avail / 1024)*100))/100;
			$avail = $avail ."G";
		} else {
			$avail = $avail ."M";
		}
	} else {
		$avail = $avail ."K";
	}

#print ":$warn:$warn_type:\n";
#print ":$crit:$crit_type:\n";
#print ":$avail:$avail_bytes:$capper:$mountpt:\n";
	$perfdata = "'" . $share . "'=" . $occupied_bytes . 'B;'
		. $warn_bytes . ';'
		. $crit_bytes . ';'
		. '0;'
		. $total_bytes;

	if ($occupied_bytes > $crit_bytes) {
		$state = "CRITICAL";
		$answer = "CRITICAL: Only $avail ($capper%) free on $mountpt";
	} elsif ( $occupied_bytes > $warn_bytes ) {
		$state = "WARNING";
		$answer = "WARNING: Only $avail ($capper%) free on $mountpt";
	} else {
		$answer = "Disk ok - $avail ($capper%) free on $mountpt";
	}
} else {
	$answer = "Result from smbclient not suitable";
	$state = "UNKNOWN";
	foreach (@lines) {
		if (/(Access denied|NT_STATUS_LOGON_FAILURE|NT_STATUS_ACCESS_DENIED)/) {
			$answer = "Access Denied";
			$state = "CRITICAL";
			last;
		}
		if (/(Unknown host \w*|Connection.*failed)/) {
			$answer = "$1";
			$state = "CRITICAL";
			last;
		}
		if (/(You specified an invalid share name|NT_STATUS_BAD_NETWORK_NAME)/) {
			$answer = "Invalid share name \\\\$host\\$share";
			$state = "CRITICAL";
			last;
		}
	}
}


print $answer;
print " | " . $perfdata if ($perfdata);
print "\n";
print "$state\n" if ($verbose);
exit $ERRORS{$state};

sub print_usage () {
	print "Usage: $PROGNAME -H <host> -s <share> -u <user> -p <password> 
      -w <warn> -c <crit> [-W <workgroup>] [-P <port>] [-a <IP>]\n";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2000 Michael Anthon/Karl DeBisschop

Perl Check SMB Disk plugin for Nagios

";
	print_usage();
	print "
-H, --hostname=HOST
   NetBIOS name of the server
-s, --share=STRING
   Share name to be tested
-W, --workgroup=STRING
   Workgroup or Domain used (Defaults to \"WORKGROUP\")
-a, --address=IP
   IP-address of HOST (only necessary if HOST is in another network)
-u, --user=STRING
   Username to log in to server. (Defaults to \"guest\")
-p, --password=STRING
   Password to log in to server. (Defaults to an empty password)
-w, --warning=INTEGER or INTEGER[kMG]
   Percent of used space at which a warning will be generated (Default: 85%)
      
-c, --critical=INTEGER or INTEGER[kMG]
   Percent of used space at which a critical will be generated (Defaults: 95%)
-P, --port=INTEGER
   Port to be used to connect to. Some Windows boxes use 139, others 445 (Defaults to smbclient default)
   
   If thresholds are followed by either a k, M, or G then check to see if that
   much disk space is available (kilobytes, Megabytes, Gigabytes)

   Warning percentage should be less than critical
   Warning (remaining) disk space should be greater than critical.

";
	support();
}
