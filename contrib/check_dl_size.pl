#! /usr/bin/perl -wT

# (c)2001 Patrick Greenwell, Stealthgeeks, LLC. (patrick@stealthgeeks.net)
# Licensed under the GNU GPL
# http://www.gnu.org/licenses/gpl.html
#
# check_dl_size: Attempts to download a specified file via FTP and verify
# it is a specified size. 
# Requires Net::FTP

# Version 1.0
# Last Updated: 8/31/01


BEGIN {
	if ($0 =~ m/^(.*?)[\/\\]([^\/\\]+)$/) {
		$runtimedir = $1;
		$PROGNAME = $2;
	}
}

require 5.004;
use strict;
use Getopt::Long;
use vars qw($opt_H $opt_f $opt_s $opt_t $verbose $PROGNAME);
use lib $main::runtimedir;
use utils qw($TIMEOUT %ERRORS &print_revision &usage &support &is_error);
use Net::FTP;

sub help ();
sub print_help ();
sub print_usage ();
sub version ();
sub display_res($$);
my ($ftpfile, $ftpdir, $filesize, $answer) = ();
my $state = $ERRORS{'UNKNOWN'};

# Directory to place file. If your machine is not secure DO NOT USE /tmp.
my $dir = "/usr/local/netsaint/etc/tmp";

# Username for login
my $user = "anonymous";

# Password (PLEASE TAKE APPROPRIATE PRECAUTIONS TO SECURE THIS)
my $pass = "guest\@example.com";


delete @ENV{'PATH', 'IFS', 'CDPATH', 'ENV', 'BASH_ENV'};

Getopt::Long::Configure('bundling', 'no_ignore_case');
GetOptions
	("V|version"    => \&version,
	 "h|help"       => \&help,
	 "v|verbose"    => \$verbose,
	 "H|hostname=s" => \$opt_H,
         "f|filename=s" => \$opt_f,
         "s|filesize=s" => \$opt_s,
         "t|timeout=s"  => \$opt_t,
         );

($opt_H) || ($opt_H = shift) || usage("Host address or name not specified\n");
my $host = $1 
        if ($opt_H =~ m/^(([0-9]{1,3}\.){3}[0-9]{1,3}|(([a-z0-9]+(\-+[a-z0-9]+)*|\.))+[a-z])$/i); 
	usage("Please provide a valid IP address or host name\n") unless ($host);

($opt_f) || ($opt_f = shift) || usage("File name not specified\n");

if ($opt_f =~ m/^(.*?)[\/\\]([^\/\\]+)$/) {
        $ftpdir = $1;
        $ftpfile = $2;
}

($opt_s) || ($opt_s = shift) || usage("File size not specified\n");
usage("File size must be numeric value") unless ($opt_s =~ m/^[0-9]+$/);

(($opt_t) && ($TIMEOUT = $opt_t)) || ($TIMEOUT = 120);
usage("TIMEOUT must be numeric value") unless ($TIMEOUT =~ m/^[0-9]+$/);

# Don't hang if there are timeout issues
$SIG{'ALRM'} = sub {
	print ("ERROR: No response from ftp server (alarm)\n");
	exit $ERRORS{'UNKNOWN'};
};
alarm($TIMEOUT);

# Make certain temporary directory exists

if ( ! -e "$dir" ) {
        display_res("Temporary directory $dir does not exist.\n", "CRITICAL");

}

# Remove existing file if any

if ( -e "$dir/$ftpfile") {
          unlink "$dir/$ftpfile"  or 
          display_res("Can't remove existing file $dir/$ftpfile.\n", "CRITICAL");
}
    
# Snarf file

my $ftp = Net::FTP->new("$host", Passive => 1, Timeout => $TIMEOUT) or
          display_res("Can't connect to $host.\n", "CRITICAL");
          $ftp->login("$user","$pass") or 
                  display_res("Login to $host failed", "CRITICAL");
          $ftp->cwd("$ftpdir") or 
                  display_res("Can't change to directory $ftpdir.\n", "CRITICAL");
          $ftp->get($ftpfile, "$dir/$ftpfile") or 
                  display_res("Can't retrieve file $ftpfile.\n", "CRITICAL");
          $ftp->quit;

# If file exists and is correct size we are happy.

if (( -e "$dir/$ftpfile" ) && (($filesize = -s "/tmp/$ftpfile") eq $opt_s)) {
        display_res("File $ftpfile size OK: $filesize bytes.\n", "OK");        
        } else {
# Otherwise we are not happy.
        display_res("File $ftpfile size incorrect: $filesize bytes", "CRITICAL");
}

exit;

sub print_usage () {
	print "Usage: $PROGNAME -H <host> -f <filename> -s <file size in bytes> -t <timeout> \n";
}

sub print_help () {
	print_revision($PROGNAME,'$ Revision: 1.0 $ ');
	print "Copyright (c) 2001 Patrick Greenwell, Stealthgeeks, LLC.\n\n";
	print_usage();
	support();
}

sub version () {
	print_revision($PROGNAME,'$ Revision: 1.0 $ ');
	exit $ERRORS{'OK'};
}

sub help () {
	print_help();
	exit $ERRORS{'OK'};
}

sub display_res ($$) {
        my ($answer, $state) = @_;
        print $answer;
        exit $ERRORS{$state};
}
