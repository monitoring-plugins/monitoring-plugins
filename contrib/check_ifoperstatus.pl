#!/usr/bin/perl -w
#
# check_ifoperstatus.pl - nagios plugin 
# 
#
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
my $TIMEOUT = 15;

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
my $snmpkey = 1;
my $community = "public";
my $port = 161;
my @snmpoids;
my $snmpIfOperStatus;
my $snmpLocIfDescr;
my $hostname;
my $session;
my $error;
my $response;


sub usage {
  printf "\nMissing arguments!\n";
  printf "\n";
  printf "Perl Check IfOperStatus plugin for Nagios\n";
  printf  "checks operational status of specified interface\n";
  printf "usage: \n";
  printf "ifoperstatus.pl -k <IF_KEY> -c <READCOMMUNITY> -p <PORT> <HOSTNAME>";
  printf "\nCopyright (C) 2000 Christoph Kron\n";
  printf "check_ifoperstatus.pl comes with ABSOLUTELY NO WARRANTY\n";
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


$status = GetOptions("key=i",\$snmpkey,
                     "community=s",\$community,
                     "port=i",\$port);
if ($status == 0)
{
        &usage;
}
  
   #shift;
   $hostname  = shift || &usage;

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

   $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8' . "." . $snmpkey;
   $snmpLocIfDescr = '1.3.6.1.4.1.9.2.2.1.1.28' . "." . $snmpkey;


   push(@snmpoids,$snmpIfOperStatus);
   push(@snmpoids,$snmpLocIfDescr);

   if (!defined($response = $session->get_request(@snmpoids))) {
      $answer=$session->error;
      $session->close;
      $state = 'CRITICAL';
      print ("$state: $answer,$community,$snmpkey");
      exit $ERRORS{$state};
   }

   $answer = sprintf("host '%s',%s(%s) is %s\n", 
      $hostname, 
      $response->{$snmpLocIfDescr},
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

