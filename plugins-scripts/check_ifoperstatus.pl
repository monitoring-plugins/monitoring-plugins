#!/usr/local/bin/perl -w
#
# check_ifoperstatus.pl - nagios plugin 
#
# Copyright (C) 2000 Christoph Kron
# Modified 5/2002 to conform to updated Nagios Plugin Guidelines
# Added support for named interfaces per Valdimir Ivaschenko (S. Ghosh)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
#
#
# Report bugs to: ck@zet.net, nagiosplug-help@lists.sf.net
#
# 11.01.2000 Version 1.0
# $Id$

use POSIX;
use strict;
use lib utils.pm ;
use utils qw($TIMEOUT %ERRORS &print_revision &support);

use Net::SNMP;
use Getopt::Long;
&Getopt::Long::config('bundling');

my $PROGNAME = "check_ifoperstatus";
my $status;
my %ifOperStatus = 	('1','up',
			 '2','down',
			 '3','testing',
			 '4','unknown',
			 '5','dormant',
			 '6','notPresent');

my $state = "UNKNOWN";
my $answer = "";
my $snmpkey = 0;
my $community = "public";
my $port = 161;
my @snmpoids;
my $snmpIfDescr = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8';
my $snmpIfName = '1.3.6.1.2.1.31.1.1.1.1';
my $snmpIfAlias = '1.3.6.1.2.1.31.1.1.1.18';
my $snmpLocIfDescr = '1.3.6.1.4.1.9.2.2.1.1.28';
my $hostname;
my $session;
my $error;
my $response;
my $snmp_version = 1 ;
my $ifXTable;
my $opt_h ;
my $opt_V ;
my $ifdescr;
my $key;



# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No snmp response from $hostname (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
#alarm($TIMEOUT);


$status = GetOptions(
			"V"   => \$opt_V, "version"    => \$opt_V,
			"h"   => \$opt_h, "help"       => \$opt_h,
			"v=i" => \$snmp_version, "snmp_version=i"  => \$snmp_version,
			"C=s" =>\$community, "community=s" => \$community,
			"k=i" =>\$snmpkey, "key=i",\$snmpkey,
			"d=s" =>\$ifdescr, "descr=s" => \$ifdescr,
			"p=i" =>\$port,  "port=i",\$port,
			"H=s" => \$hostname, "hostname=s" => \$hostname,
			"I"	  => \$ifXTable, "ifmib" => \$ifXTable);


				
if ($status == 0)
{
	print_help();
	exit $ERRORS{'OK'};
}
  
if ($opt_V) {
	print_revision($PROGNAME,'$Revision$ ');
	exit $ERRORS{'OK'};
}

if ($opt_h) {
	print_help();
	exit $ERRORS{'OK'};
}

if (! utils::is_hostname($hostname)){
	usage();
	exit $ERRORS{"UNKNOWN"};
}


if ( $snmp_version =~ /[12]/ ) {
   ($session, $error) = Net::SNMP->session(
		-hostname  => $hostname,
		-community => $community,
		-port      => $port,
		-version	=> $snmp_version
	);

	if (!defined($session)) {
		$state='UNKNOWN';
		$answer=$error;
		print ("$state: $answer");
		exit $ERRORS{$state};
	}
}elsif ( $snmp_version =~ /3/ ) {
	$state='UNKNOWN';
	print ("$state: No support for SNMP v3 yet\n");
	exit $ERRORS{$state};
}else{
	$state='UNKNOWN';
	print ("$state: No support for SNMP v$snmp_version yet\n");
	exit $ERRORS{$state};
}

if (defined $ifdescr) {
	# escape "/" in ifdescr - very common in the Cisco world
	$ifdescr =~ s/\//\\\//g;

	$status=fetch_ifdescr();  # if using on device with large number of interfaces
							  # recommend use of SNMP v2 (get-bulk)
	if ($status==0) {
		$state = "UNKNOWN";
		printf "$state: could not retrive ifIndex - $status-$snmpkey\n";
		$session->close;
		exit $ERRORS{$state};
	}
}
if ( $snmpkey == 0 ) {
	printf "ifIndex key cannot be 0\n";
	usage();
	exit $ERRORS{'UNKNOWN'};
}

   $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8' . "." . $snmpkey;
   $snmpIfDescr = '1.3.6.1.2.1.2.2.1.2' . "." . $snmpkey;
   $snmpIfAlias = '1.3.6.1.2.1.31.1.1.1.18' . "." . $snmpkey ; 


push(@snmpoids,$snmpIfOperStatus);
push(@snmpoids,$snmpIfDescr);
push(@snmpoids,$snmpIfAlias) if (defined $ifXTable) ;

   if (!defined($response = $session->get_request(@snmpoids))) {
      $answer=$session->error;
      $session->close;
      $state = 'CRITICAL';
      print ("$state: $answer for ifIndex $snmpkey\n");
      exit $ERRORS{$state};
   }

   $answer = sprintf("host '%s', %s(%s) is %s\n", 
      $hostname, 
      $response->{$snmpIfDescr},
      $snmpkey, 
      $ifOperStatus{$response->{$snmpIfOperStatus}}
   );

   $session->close;

   if ( $response->{$snmpIfOperStatus} == 1 ) {
      $state = 'OK';
   }
   else {
	$state = 'CRITICAL';
   }

print ("$state: $answer");
exit $ERRORS{$state};


sub fetch_ifdescr {
	if (!defined ($response = $session->get_table($snmpIfDescr))) {
		$answer=$session->error;
		$session->close;
		$state = 'CRITICAL';
		printf ("$state: $answer for $snmpIfDescr  with snmp version $snmp_version\n");
		$session->close;
		exit $ERRORS{$state};
	}
	
	foreach $key ( keys %{$response}) {
		if ($response->{$key} =~ /$ifdescr/) {
			$key =~ /.*\.(\d+)$/;
			$snmpkey = $1;
			#print "$ifdescr = $key / $snmpkey \n";  #debug
		}
	}
	unless (defined $snmpkey) {
		$session->close;
		$state = 'CRITICAL';
		printf "$state: Could not match $ifdescr on $hostname\n";
		exit $ERRORS{$state};
	}
	
	return $snmpkey;
}

sub usage {
  printf "\nMissing arguments!\n";
  printf "\n";
  printf "usage: \n";
  printf "check_ifoperstatus -k <IF_KEY> -H <HOSTNAME> [-C <community>]\n";
  printf "Copyright (C) 2000 Christoph Kron\n";
  printf "check_ifoperstatus.pl comes with ABSOLUTELY NO WARRANTY\n";
  printf "This programm is licensed under the terms of the ";
  printf "GNU General Public License\n(check source code for details)\n";
  printf "\n\n";
  exit $ERRORS{"UNKNOWN"};
}

sub print_help {
	printf "check_ifoperstatus plugin for Nagios monitors operational \n";
  	printf "status of a particular network interface on the target host\n";
	printf "\nUsage:\n";
	printf "   -H (--hostname)   Hostname to query - (required)\n";
	printf "   -C (--community)  SNMP read community (defaults to public,\n";
	printf "                     used with SNMP v1 and v2c\n";
	printf "   -v (--snmp_version)  1 for SNMP v1 (default)\n";
	printf "                        2 for SNMP v2c\n";
	printf "                        SNMP v2c will use get_bulk for less overhead\n";
	printf "                        if monitoring with -d\n";
	printf "   -k (--key)        SNMP ifIndex value\n";
	printf "   -d (--descr)      SNMP ifDescr value\n";
	printf "   -p (--port)       SNMP port (default 161)\n";
	printf "   -I (--ifmib)      Agent supports IFMIB ifXTable.  Do not use if\n";
	printf "                     you don't know what this is.\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	printf " -k or -d must be specified\n\n";
	print_revision($PROGNAME, '$Revision$');
	
}
