#!/usr/bin/perl -w
#
# check_ifstatus.pl - nagios plugin 
# 
#
# Copyright (C) 2000 Christoph Kron
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
# Report bugs to: ck@zet.net
#
# 11.01.2000 Version 1.0

use strict;

use Net::SNMP;
use Getopt::Long;
&Getopt::Long::config('auto_abbrev');


my $status;
my $TIMEOUT = 1500;

my %ERRORS = ('UNKNOWN' , '-1',
              'OK' , '0',
              'WARNING', '1',
              'CRITICAL', '2');

my %ifOperStatus = 	('1','up',
			 '2','down',
			 '3','testing',
			 '4','unknown',
			 '5','dormant',
			 '6','notPresent');

my $state = "UNKNOWN";
my $answer = "";
my $snmpkey;
my $snmpoid;
my $key;
my $community = "public";
my $port = 161;
my @snmpoids;
my $snmpIfAdminStatus = '1.3.6.1.2.1.2.2.1.7';
my $snmpIfDescr = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8';
my $snmpLocIfDescr = '1.3.6.1.4.1.9.2.2.1.1.28';
my $hostname;
my $session;
my $error;
my $response;
my %ifStatus;
my $ifup =0 ;
my $ifdown =0;
my $ifdormant = 0;
my $ifmessage;

sub usage {
  printf "\nMissing arguments!\n";
  printf "\n";
  printf "Perl Check IfStatus plugin for Nagios\n";
  printf "monitors operational status of each interface\n";
  printf "usage: \n";
  printf "check_ifstatus.pl -c <READCOMMUNITY> -p <PORT> <HOSTNAME>\n";
  printf "Copyright (C) 2000 Christoph Kron\n";
  printf "check_ifstatus.pl comes with ABSOLUTELY NO WARRANTY\n";
  printf "This programm is licensed under the terms of the ";
  printf "GNU General Public License\n(check source code for details)\n";
  printf "\n\n";
  exit $ERRORS{"UNKNOWN"};
}

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: No snmp response from $hostname (alarm)\n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);


$status = GetOptions("community=s",\$community,
                     "port=i",\$port);
if ($status == 0)
{
        &usage;
}
  
   #shift;
   $hostname  = shift || &usage;



   push(@snmpoids,$snmpIfOperStatus);
   push(@snmpoids,$snmpLocIfDescr);
   push(@snmpoids,$snmpIfAdminStatus);
   push(@snmpoids,$snmpIfDescr);

foreach $snmpoid (@snmpoids) {

   ($session, $error) = Net::SNMP->session(
      -hostname  => $hostname,
      -community => $community,
      -port      => $port
   );

   if (!defined($session)) {
      $state='UNKNOWN';
      $answer=$error;
      print ("$state: $answer");
      exit $ERRORS{$state};
   }

   if (!defined($response = $session->get_table($snmpoid))) {
      $answer=$session->error;
      $session->close;
      $state = 'CRITICAL';
      print ("$state: $answer,$community,$snmpkey");
      exit $ERRORS{$state};
   }

   foreach $snmpkey (keys %{$response}) {
      $snmpkey =~ /.*\.(\d+)$/;
      $key = $1;
      $ifStatus{$key}{$snmpoid} = $response->{$snmpkey};
   }
   $session->close;
}

   foreach $key (keys %ifStatus) {
      # check only if interface is administratively up
      if ($ifStatus{$key}{$snmpIfAdminStatus} == 1 ) {
         if ($ifStatus{$key}{$snmpIfOperStatus} == 1 ) { $ifup++ ;}
         if ($ifStatus{$key}{$snmpIfOperStatus} == 2 ) {
             $ifdown++ ;
             $ifmessage .= sprintf("%s: down -> %s<BR>",
                                 $ifStatus{$key}{$snmpIfDescr},
				 $ifStatus{$key}{$snmpLocIfDescr});

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

