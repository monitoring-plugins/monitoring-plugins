#!@PERL@ -w
#


use strict;
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);
use vars qw($PROGNAME);
use Getopt::Long;
use vars qw($opt_V $opt_h $verbose $opt_w $opt_c $opt_H);
my (@test, $low1, $med1, $high1, $snr, $low2, $med2, $high2);
my ($low, $med, $high, $lowavg, $medavg, $highavg, $tot, $ss);

$PROGNAME = "check_wave";
sub print_help ();
sub print_usage ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}='';
$ENV{'ENV'}='';

Getopt::Long::Configure('bundling');
GetOptions
	("V"   => \$opt_V, "version"    => \$opt_V,
	 "h"   => \$opt_h, "help"       => \$opt_h,
	 "v" => \$verbose, "verbose"  => \$verbose,
	 "w=s" => \$opt_w, "warning=s"  => \$opt_w,
	 "c=s" => \$opt_c, "critical=s" => \$opt_c,
	 "H=s" => \$opt_H, "hostname=s" => \$opt_H);

if ($opt_V) {
	print_revision($PROGNAME,'@NP_VERSION@'); #'
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

$opt_H = shift unless ($opt_H);
print_usage() unless ($opt_H);
my $host = $1 if ($opt_H =~ m/^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|[a-zA-Z][-a-zA-Z0]+(\.[a-zA-Z][-a-zA-Z0]+)*)$/);
print_usage() unless ($host);

($opt_c) || ($opt_c = shift) || ($opt_c = 120);
my $critical = $1 if ($opt_c =~ /([0-9]+)/);

($opt_w) || ($opt_w = shift) || ($opt_w = 60);
my $warning = $1 if ($opt_w =~ /([0-9]+)/);

$low1 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.8.1`;
@test = split(/ /,$low1);
$low1 = $test[2];

$med1 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.9.1`;
@test = split(/ /,$med1);
$med1 = $test[2];

$high1 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.10.1`;
@test = split(/ /,$high1);
$high1 = $test[2];

sleep(2);

$snr = `snmpget $host public .1.3.6.1.4.1.762.2.5.2.1.17.1`;
@test = split(/ /,$snr);
$snr = $test[2];
$snr = int($snr*25);

$low2 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.8.1`;
@test = split(/ /,$low2);
$low2 = $test[2];

$med2 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.9.1`;
@test = split(/ /,$med2);
$med2 = $test[2];

$high2 = `snmpget $host public .1.3.6.1.4.1.74.2.21.1.2.1.10.1`;
@test = split(/ /,$high2);
$high2 = $test[2];

$low = $low2 - $low1;
$med = $med2 - $med1;
$high = $high2 - $high1;

$tot = $low + $med + $high;

if ($tot==0) {
	$ss = 0;
} else {
	$lowavg = $low / $tot;
	$medavg = $med / $tot;
	$highavg = $high / $tot;
	$ss = ($medavg*50) + ($highavg*100);
}

printf("Signal Strength at: %3.0f%,  SNR at $snr%",$ss);

if ($ss<$critical) {
	exit(2);
} elsif ($ss<$warning) {
	exit(1);
} else {
	exit(0);
}


sub print_usage () {
	print "Usage: $PROGNAME -H <host> [-w <warn>] [-c <crit>]\n";
}

sub print_help () {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2000 Jeffery Blank/Karl DeBisschop\n";
	print "\n";
	print_usage();
	print "\n";
	print "<warn> = Signal strength at which a warning message will be generated.\n";
	print "<crit> = Signal strength at which a critical message will be generated.\n\n";
	support();
}
