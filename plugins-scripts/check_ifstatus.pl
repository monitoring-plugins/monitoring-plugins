#!/usr/local/bin/perl -w
#
# check_ifstatus.pl - nagios plugin 
# 
#
# Copyright (C) 2000 Christoph Kron
# Modified 5/2002 to conform to updated Nagios Plugin Guidelines (S. Ghosh)
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
#
# $Id$

use POSIX;
use strict;
use lib utils.pm ;
use utils qw($TIMEOUT %ERRORS &print_revision &support);

use Net::SNMP;
use Getopt::Long;
Getopt::Long::Configure('bundling');

my $PROGNAME = "check_ifstatus";


my $status;
my %ifOperStatus = 	('1','up',
			 '2','down',
			 '3','testing',
			 '4','unknown',
			 '5','dormant',
			 '6','notPresent');

my $state = "UNKNOWN";
my $answer = "";
my $snmpkey=0;
my $snmpoid=0;
my $key=0;
my $community = "public";
my $port = 161;
my @snmpoids;
my $snmpIfAdminStatus = '1.3.6.1.2.1.2.2.1.7';
my $snmpIfDescr = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8';
my $snmpIfName = '1.3.6.1.2.1.31.1.1.1.1';
my $snmpIfAlias = '1.3.6.1.2.1.31.1.1.1.18';
my $snmpLocIfDescr = '1.3.6.1.4.1.9.2.2.1.1.28';
my $hostname;
my $session;
my $error;
my $response;
my %ifStatus;
my $ifup =0 ;
my $ifdown =0;
my $ifdormant = 0;
my $ifmessage = "";
my $snmp_version = 1;
my $ifXTable;
my $opt_h ;
my $opt_V ;





# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No snmp response from $hostname (alarm timeout)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);



#Option checking
$status = GetOptions(
		"V"   => \$opt_V, "version"    => \$opt_V,
		"h"   => \$opt_h, "help"       => \$opt_h,
		"v=i" => \$snmp_version, "snmp_version=i"  => \$snmp_version,
		"C=s" =>\$community,"community=s" => \$community,
		"p=i" =>\$port, "port=i" => \$port,
		"H=s" => \$hostname, "hostname=s" => \$hostname,
		"I"	  => \$ifXTable, "ifmib" => \$ifXTable );
		
if ($status == 0)
{
	print_help() ;
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

if ( ! $snmp_version ) {
	$snmp_version =1 ;
}else{
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
}



push(@snmpoids,$snmpIfOperStatus);
push(@snmpoids,$snmpIfAdminStatus);
push(@snmpoids,$snmpIfDescr);
push(@snmpoids,$snmpIfName) if ( defined $ifXTable);



foreach $snmpoid (@snmpoids) {

   if (!defined($response = $session->get_table($snmpoid))) {
      $answer=$session->error;
      $session->close;
      $state = 'CRITICAL';
      print ("$state: $answer for $snmpoid  with snmp version $snmp_version\n");
      exit $ERRORS{$state};
   }

   foreach $snmpkey (keys %{$response}) {
      $snmpkey =~ /.*\.(\d+)$/;
      $key = $1;
      $ifStatus{$key}{$snmpoid} = $response->{$snmpkey};
   }
}


$session->close;

foreach $key (keys %ifStatus) {

	# check only if interface is administratively up
    if ($ifStatus{$key}{$snmpIfAdminStatus} == 1 ) {
    	if ($ifStatus{$key}{$snmpIfOperStatus} == 1 ) { $ifup++ ;}
        if ($ifStatus{$key}{$snmpIfOperStatus} == 2 ) {
             $ifdown++ ;
             $ifmessage .= sprintf("%s: down -> %s<BR>",
                                 $ifStatus{$key}{$snmpIfDescr},
								 $ifStatus{$key}{$snmpIfName});

         }
         if ($ifStatus{$key}{$snmpIfOperStatus} == 5 ) { $ifdormant++ ;}
      }
   }
   

   if ($ifdown > 0) {
      $state = 'CRITICAL';
      $answer = sprintf("host '%s', interfaces up: %d, down: %d, dormant: %d<BR>",
                        $hostname,
			$ifup,
			$ifdown,
			$ifdormant);
      $answer = $answer . $ifmessage . "\n";
   }
   else {
      $state = 'OK';
      $answer = sprintf("host '%s', interfaces up: %d, down: %d, dormant: %d\n",
                        $hostname,
			$ifup,
			$ifdown,
			$ifdormant);
   }

print ("$state: $answer");
exit $ERRORS{$state};


sub usage {
	printf "\nMissing arguments!\n";
	printf "\n";
	printf "check_ifstatus -C <READCOMMUNITY> -p <PORT> -H <HOSTNAME>\n";
	printf "Copyright (C) 2000 Christoph Kron\n";
	printf "Updates 5/2002 Subhendu Ghosh\n";
	printf "\n\n";
	support();
	exit $ERRORS{"UNKNOWN"};
}

sub print_help {
	printf "check_ifstatus plugin for Nagios monitors operational \n";
  	printf "status of each network interface on the target host\n";
	printf "\nUsage:\n";
	printf "   -H (--hostname)   Hostname to query - (required)\n";
	printf "   -C (--community)  SNMP read community (defaults to public,\n";
	printf "                     used with SNMP v1 and v2c\n";
	printf "   -v (--snmp_version)  1 for SNMP v1 (default)\n";
	printf "                        2 for SNMP v2c\n";
	printf "                        SNMP v2c will use get_bulk for less overhead\n";
	printf "   -p (--port)       SNMP port (default 161)\n";
	printf "   -I (--ifmib)      Agent supports IFMIB ifXTable.  Do not use if\n";
	printf "                     you don't know what this is.\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	print_revision($PROGNAME, '$Revision$');
	
}
