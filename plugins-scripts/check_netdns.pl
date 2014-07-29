#!@PERL@ -w

# Perl version of check_dns plugin which calls DNS directly instead of
# relying on nslookup (which has bugs)
#
# Copyright 2000, virCIO, LLP
#
# Revision 1.3  2002/05/07 05:35:49  sghosh
# 2nd fix for ePN
#
# Revision 1.2  2002/05/02 16:43:29  sghosh
# fix for embedded perl
#
# Revision 1.1.1.1  2002/02/28 06:43:00  egalstad
# Initial import of existing plugin code
#
# Revision 1.1  2000/08/03 20:41:12  karldebisschop
# rename to avoid conflict when installing
#
# Revision 1.1  2000/08/03 19:27:08  karldebisschop
# use Net::DNS to check name server
#
# Revision 1.1  2000/07/20 19:09:13  cwg
# All the pieces needed to use my version of check_dns.
#
# 

use Getopt::Long;
use Net::DNS;
use FindBin;
use lib "$FindBin::Bin";
use lib '@libexecdir@';
use utils ;

my $PROGNAME = "check_netdns";

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

Getopt::Long::Configure(`bundling`);
GetOptions("V" => $opt_V,         "version" => $opt_V,
					 "h" => $opt_h,         "help" => $opt_h,
					 "t=i" => $opt_t,       "timeout=i" => $opt_t,
					 "s=s" => $opt_s,       "server=s" => $opt_s,
					 "H=s" => $opt_H,       "hostname=s" => $opt_H);
                           
# -h means display verbose help screen
if($opt_h){ print_help(); exit 0; }

# -V means display version number
if ($opt_V) { print_version(); exit 0; }

# -H means host name
$opt_H = shift unless ($opt_H);
unless ($opt_H) { print_usage(); exit -1; }
if ($opt_H && 
		$opt_H =~ m/^([0-9]+.[0-9]+.[0-9]+.[0-9]+|[a-zA-Z][-a-zA-Z0]+(.[a-zA-Z][-a-zA-Z0]+)*)$/)
{
	$host = $1;
} else {
	print "$opt_H is not a valid host name";
	exit -1;
}

# -s means server name
$opt_s = shift unless ($opt_s);
if ($opt_s) {
	if ($opt_s =~ m/^([0-9]+.[0-9]+.[0-9]+.[0-9]+|[a-zA-Z][-a-zA-Z0]+(.[a-zA-Z][-a-zA-Z0]+)*)$/)
	{
		$server = $1;
	} else {
		print "$opt_s is not a valid host name";
		exit -1;
	}
}

# -t means timeout
my $timeout = 10 unless ($opt_t);

my $res = new Net::DNS::Resolver;
#$res->debug(1);
if ($server) {
	$res->nameservers($server);
}

$res->tcp_timeout($timeout);
$SIG{ALRM} = &catch_alarm;
alarm($timeout);

$query = $res->query($host);
if ($query) {
	my @answer = $query->answer;
	if (@answer) {
		print join(`/`, map {
			$_->type . ` ` . $_->rdatastr;
		} @answer);
		exit 0;
	} else {
		print "empty answer";
		exit 2;
	}
}
else {
	print "query failed: ", $res->errorstring, "";
	exit 2;
}

sub catch_alarm {
	print "query timed out";
	exit 2;
}

sub print_version () {
	my $arg0 =  $0;
	chomp $arg0;
	print "$arg0                        version 0.1";
}
sub print_help() {
	print_version();
	print "";
	print "Check if a nameserver can resolve a given hostname.";
	print "";
	print_usage();
	print "";
	print "-H, --hostname=HOST";
	print "   The name or address you want to query";
	print "-s, --server=HOST";
	print "   Optional DNS server you want to use for the lookup";
	print "-t, --timeout=INTEGER";
	print "   Seconds before connection times out (default: 10)";
	print "-h, --help";
	print "   Print detailed help";
	print "-V, --version";
	print "   Print version numbers and license information";
}

sub print_usage () {
	my $arg0 = $0;
	chomp $arg0;
	print "$arg0 check_dns -H host [-s server] [-t timeout]";
	print "$arg0 [-h | --help]";
	print "$arg0 [-V | --version]";
}

