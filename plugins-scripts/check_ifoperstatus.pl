#!/usr/local/bin/perl -w
#
# check_ifoperstatus.pl - nagios plugin 
#
# Copyright (C) 2000 Christoph Kron,
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
# Report bugs to:  nagiosplug-help@lists.sourceforge.net
#
# 11.01.2000 Version 1.0
# $Id$
#
# Patches from Guy Van Den Bergh to warn on ifadminstatus down interfaces
# instead of critical.
#
# Primary MIB reference - RFC 2863


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
			 '6','notPresent',
			 '7','lowerLayerDown');  # down due to the state of lower layer interface(s)

my $state = "UNKNOWN";
my $answer = "";
my $snmpkey = 0;
my $community = "public";
my $port = 161;
my @snmpoids;
my $sysUptime        = '1.3.6.1.2.1.1.3.0';
my $snmpIfDescr      = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfAdminStatus = '1.3.6.1.2.1.2.2.1.7';
my $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8';
my $snmpIfName       = '1.3.6.1.2.1.31.1.1.1.1';
my $snmpIfLastChange = '1.3.6.1.2.1.2.2.1.9';
my $snmpIfAlias      = '1.3.6.1.2.1.31.1.1.1.18';
my $snmpLocIfDescr   = '1.3.6.1.4.1.9.2.2.1.1.28';
my $hostname;
my $ifName;
my $session;
my $error;
my $response;
my $snmp_version = 1 ;
my $ifXTable;
my $opt_h ;
my $opt_V ;
my $ifdescr;
my $key;
my $lastc;
my $dormantWarn;
my $name;



# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No snmp response from $hostname (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
#alarm($TIMEOUT);


### Validate Arguments

$status = GetOptions(
			"V"   => \$opt_V, "version"    => \$opt_V,
			"h"   => \$opt_h, "help"       => \$opt_h,
			"v=i" => \$snmp_version, "snmp_version=i"  => \$snmp_version,
			"C=s" =>\$community, "community=s" => \$community,
			"k=i" =>\$snmpkey, "key=i",\$snmpkey,
			"d=s" =>\$ifdescr, "descr=s" => \$ifdescr,
			"l=s" => \$lastc,  "lastchange=s" => \$lastc,
			"p=i" =>\$port,  "port=i",\$port,
			"H=s" => \$hostname, "hostname=s" => \$hostname,
			"I"	  => \$ifXTable, "ifmib" => \$ifXTable,
			"n=s" => \$ifName, "name=s" => \$ifName,
			"w=s" => \$dormantWarn, "warn=s" => \$dormantWarn );


				
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


unless ($snmpkey > 0 || defined $ifdescr){
	printf "Either a valid snmpkey key (-k) or a ifDescr (-d) must be provided)\n";
	usage();
	exit $ERRORS{"UNKNOWN"};
}


if (defined $name) {
	$ifXTable=1;
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

## End validation



## map ifdescr to ifindex - should look at being able to cache this value

if (defined $ifdescr) {
	# escape "/" in ifdescr - very common in the Cisco world
	$ifdescr =~ s/\//\\\//g;

	$status=fetch_ifdescr();  # if using on device with large number of interfaces
							  # recommend use of SNMP v2 (get-bulk)
	if ($status==0) {
		$state = "UNKNOWN";
		printf "$state: could not retrive snmpkey - $status-$snmpkey\n";
		$session->close;
		exit $ERRORS{$state};
	}
}


## Main function

$snmpIfAdminStatus = $snmpIfAdminStatus . "." . $snmpkey;
$snmpIfOperStatus = $snmpIfOperStatus . "." . $snmpkey;
$snmpIfDescr = $snmpIfDescr . "." . $snmpkey;
$snmpIfName	= $snmpIfName . "." . $snmpkey ;
$snmpIfAlias = $snmpIfAlias . "." . $snmpkey ; 

push(@snmpoids,$snmpIfAdminStatus);
push(@snmpoids,$snmpIfOperStatus);
push(@snmpoids,$snmpIfDescr);
push(@snmpoids,$snmpIfName) if (defined $ifXTable) ;
push(@snmpoids,$snmpIfAlias) if (defined $ifXTable) ;

   if (!defined($response = $session->get_request(@snmpoids))) {
      $answer=$session->error;
      $session->close;
      $state = 'WARNING';
      print ("$state: SNMP error: $answer\n");
      exit $ERRORS{$state};
   }

   $answer = sprintf("host '%s', %s(%s) is %s\n", 
      $hostname, 
      $response->{$snmpIfDescr},
      $snmpkey, 
      $ifOperStatus{$response->{$snmpIfOperStatus}}
   );


   ## Check to see if ifName match is requested and it matches - exit if no match
   ## not the interface we want to monitor
   if ( defined $name && not ($response->{$snmpIfName} eq $name) ) {
      $state = 'UNKNOWN';
      $answer = "Interface name ($name) doesn't match snmp value ($response->{$snmpIfName}) (index $snmpkey)";
      print ("$state: $answer");
      exit $ERRORS{$state};
   } 

   ## define the interface name
   if (defined $ifXTable) {
     $name = $response->{$snmpIfName} ." - " .$response->{$snmpIfAlias} ; 
   }else{
     $name = $response->{$snmpIfDescr} ;
   }
   
   ## if AdminStatus is down - some one made a consious effort to change config
   ##
   if ( not ($response->{$snmpIfAdminStatus} == 1) ) {
      $state = 'WARNING';
      $answer = "Interface $name (index $snmpkey) is administratively down.";

   } 
   ## Check operational status
   elsif ( $response->{$snmpIfOperStatus} == 2 ) {
      $state = 'CRITICAL';
      $answer = "Interface $name (index $snmpkey) is down.";
   } elsif ( $response->{$snmpIfOperStatus} == 5 ) {
      if (defined $dormantWarn ) {
	    if ($dormantWarn eq "w") {
	  	  $state = 'WARNNG';
		  $answer = "Interface $name (index $snmpkey) is dormant.";
	    }elsif($dormantWarn eq "c") {
	  	  $state = 'CRITICAL';
		  $answer = "Interface $name (index $snmpkey) is dormant.";
        }elsif($dormantWarn eq "i") {
	  	  $state = 'OK';
		  $answer = "Interface $name (index $snmpkey) is dormant.";
        }
	 }else{
	    # dormant interface  - but warning/critical/ignore not requested
 	   $state = 'CRITICAL';
	   $answer = "Interface $name (index $snmpkey) is dormant.";
	}
   } elsif ( $response->{$snmpIfOperStatus} == 6 ) {
	   $state = 'CRITICAL';
	   $answer = "Interface $name (index $snmpkey) notPresent - possible hotswap in progress.";
   } elsif ( $response->{$snmpIfOperStatus} == 7 ) {
	   $state = 'CRITICAL';
	   $answer = "Interface $name (index $snmpkey) down due to lower layer being down.";

   } elsif ( $response->{$snmpIfOperStatus} == 3 || $response->{$snmpIfOperStatus} == 4  ) {
	   $state = 'CRITICAL';
	   $answer = "Interface $name (index $snmpkey) down (testing/unknown).";

   } else {
      $state = 'OK';
      $answer = "Interface $name (index $snmpkey) is up.";
   }



print ("$state: $answer");
exit $ERRORS{$state};


### subroutines

sub fetch_ifdescr {
	if (!defined ($response = $session->get_table($snmpIfDescr))) {
		$answer=$session->error;
		$session->close;
		$state = 'CRITICAL';
		printf ("$state: SNMP error with snmp version $snmp_version ($answer)\n");
		$session->close;
		exit $ERRORS{$state};
	}
	
	foreach $key ( keys %{$response}) {
		if ($response->{$key} =~ /^$ifdescr$/) {
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
	printf "   -k (--key)        SNMP IfIndex value\n";
	printf "   -d (--descr)      SNMP ifDescr value\n";
	printf "   -p (--port)       SNMP port (default 161)\n";
	printf "   -I (--ifmib)      Agent supports IFMIB ifXTable.  Do not use if\n";
	printf "                     you don't know what this is. \n";
	printf "   -n (--name)       the value should match the returned ifName\n";
	printf "                     (Implies the use of -I)\n";
	printf "   -w (--warn =i|w|c) ignore|warn|crit if the interface is dormant (default critical)\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	printf " -k or -d must be specified\n\n";
	printf "Note: either -k or -d must be specified and -d is much more network \n";
	printf "intensive.  Use it sparingly or not at all.  -n is used to match against\n";
	printf "a much more descriptive ifName value in the IfXTable to verify that the\n";
	printf "snmpkey has not changed to some other network interface after a reboot.\n\n";
	print_revision($PROGNAME, '$Revision$');
	
}
