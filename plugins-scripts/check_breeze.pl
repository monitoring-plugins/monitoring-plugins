#! /usr/bin/perl -wT

BEGIN {
	if ($0 =~ m/^(.*?)[\/\\]([^\/\\]+)$/) {
		$runtimedir = $1;
		$PROGNAME = $2;
	}
}

use strict;
use Getopt::Long;
use vars qw($opt_V $opt_h $opt_H $opt_w $opt_c $PROGNAME);
use lib $main::runtimedir;
use utils qw(%ERRORS &print_revision &support &usage);

sub print_help ();
sub print_usage ();

$ENV{'PATH'}='';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V, "version"    => \$opt_V,
	 "h"   => \$opt_h, "help"       => \$opt_h,
	 "w=s" => \$opt_w, "warning=s"  => \$opt_w,
	 "c=s" => \$opt_c, "critical=s" => \$opt_c,
	 "H=s" => \$opt_H, "hostname=s" => \$opt_H);

if ($opt_V) {
	print_revision($PROGNAME,'$Revision$');
	exit $ERRORS{'OK'};
}

if ($opt_h) {print_help(); exit $ERRORS{'OK'};}

($opt_H) || ($opt_H = shift) || usage("Host name/address not specified\n");
my $host = $1 if ($opt_H =~ /([-.A-Za-z0-9]+)/);
($host) || usage("Invalid host: $opt_H\n");

($opt_w) || ($opt_w = shift) || usage("Warning threshold not specified\n");
my $warning = $1 if ($opt_w =~ /([0-9]{1,2}|100)+/);
($warning) || usage("Invalid warning threshold: $opt_w\n");

($opt_c) || ($opt_c = shift) || usage("Critical threshold not specified\n");
my $critical = $1 if ($opt_c =~ /([0-9]{1,2}|100)/);
($critical) || usage("Invalid critical threshold: $opt_c\n");

my $sig=0;
$sig = `/usr/bin/snmpget $host public .1.3.6.1.4.1.710.3.2.3.1.3.0`;
my @test=split(/ /,$sig);
$sig=$test[2];
$sig=int($sig);
if ($sig>100){$sig=100}

print "Signal Strength at: $sig%\n";

exit $ERRORS{'CRITICAL'} if ($sig<$critical);
exit $ERRORS{'WARNING'} if ($sig<$warning);
exit $ERRORS{'OK'};


sub print_usage () {
	print "Usage: $PROGNAME -H <host> -w <warn> -c <crit>\n";
}

sub print_help () {
	print_revision($PROGNAME,'$Revision$');
	print "Copyright (c) 2000 Jeffrey Blank/Karl DeBisschop

This plugin reports the signal strength of a Breezecom wireless equipment

";
	print_usage();
	print "
-H, --hostname=HOST
   Name or IP address of host to check
-w, --warning=INTEGER
   Percentage strength below which a WARNING status will result
-c, --critical=INTEGER
   Percentage strength below which a CRITICAL status will result

";
	support();
}
