#!@PERL@ -w
#
# check_ifstatus.pl - monitoring plugin
# 
#
# Copyright (C) 2000 Christoph Kron
# Modified 5/2002 to conform to updated Monitoring Plugins Guidelines (S. Ghosh)
#  Added -x option (4/2003)
#  Added -u option (4/2003)
#  Added -M option (10/2003)
#  Added SNMPv3 support (10/2003)
#  Added -n option (07/2014)
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
# Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
#
#
# Report bugs to: ck@zet.net, help@monitoring-plugins.org
# 
# 11.01.2000 Version 1.0
#

use POSIX;
use strict;
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);

use Net::SNMP;
use Getopt::Long;
Getopt::Long::Configure('bundling');

my $PROGNAME = "check_ifstatus";

sub print_help ();
sub usage ($);
sub print_usage ();
sub process_arguments ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

my $status;
my %ifOperStatus =	('1','up',
			 '2','down',
			 '3','testing',
			 '4','unknown',
			 '5','dormant',
			 '6','notPresent',
			 '7','lowerLayerDown');  # down due to the state of lower layer interface(s));

my $timeout ;
my $state = "UNKNOWN";
my $answer = "";
my $snmpkey=0;
my $snmpoid=0;
my $key=0;
my $community = "public";
my $maxmsgsize = 1472 ; # Net::SNMP default is 1472
my ($seclevel, $authproto, $secname, $authpass, $privpass, $privproto, $auth, $priv, $context);
my $port = 161;
my @snmpoids;
my $snmpIfAdminStatus = '1.3.6.1.2.1.2.2.1.7';
my $snmpIfDescr = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfOperStatus = '1.3.6.1.2.1.2.2.1.8';
my $snmpIfName = '1.3.6.1.2.1.31.1.1.1.1';
my $snmpIfAlias = '1.3.6.1.2.1.31.1.1.1.18';
my $snmpLocIfDescr = '1.3.6.1.4.1.9.2.2.1.1.28';
my $snmpIfType = '1.3.6.1.2.1.2.2.1.3';
my $hostname;
my $session;
my $error;
my $response;
my %ifStatus;
my $ifup =0 ;
my $ifdown =0;
my $ifdormant = 0;
my $ifexclude = 0 ;
my $ifunused = 0;
my $ifmessage = "";
my $snmp_version = 1;
my $ifXTable;
my $opt_h ;
my $opt_V ;
my $opt_u;
my $opt_n;
my $opt_x ;
my %excluded ;
my %unused_names ;
my @unused_ports ;
my %session_opts;





# Just in case of problems, let's not hang the monitoring system
$SIG{'ALRM'} = sub {
     print ("ERROR: No snmp response from $hostname (alarm timeout)\n");
     exit $ERRORS{"UNKNOWN"};
};


#Option checking
$status = process_arguments();

if ($status != 0)
{
	print_help() ;
	exit $ERRORS{'UNKNOWN'};
}


alarm($timeout);
($session, $error) = Net::SNMP->session(%session_opts);
		
if (!defined($session)) {
			$state='UNKNOWN';
			$answer=$error;
			print ("$state: $answer\n");
			exit $ERRORS{$state};
}


push(@snmpoids,$snmpIfOperStatus);
push(@snmpoids,$snmpIfAdminStatus);
push(@snmpoids,$snmpIfDescr);
push(@snmpoids,$snmpIfType);
push(@snmpoids,$snmpIfName) if ( defined $ifXTable);
push(@snmpoids,$snmpIfAlias) if ( defined $ifXTable);




foreach $snmpoid (@snmpoids) {

   if (!defined($response = $session->get_table($snmpoid))) {
      $answer=$session->error;
      $session->close;
      $state = 'CRITICAL';
			if ( ( $snmpoid =~ $snmpIfName ) && defined $ifXTable ) {
				print ("$state: Device does not support ifTable - try without -I option\n");
			}else{
				print ("$state: $answer for $snmpoid  with snmp version $snmp_version\n");
			}
      exit $ERRORS{$state};
   }

   foreach $snmpkey (keys %{$response}) {
      $snmpkey =~ /.*\.(\d+)$/;
      $key = $1;
      $ifStatus{$key}{$snmpoid} = $response->{$snmpkey};
   }
}


$session->close;

alarm(0);

foreach $key (keys %ifStatus) {

	# skip unused interfaces
	my $ifName = $ifStatus{$key}{$snmpIfDescr};

	if (!defined($ifStatus{$key}{'notInUse'}) && !grep(/^${ifName}/, @unused_ports )) {
		# check only if interface is administratively up
		if ($ifStatus{$key}{$snmpIfAdminStatus} == 1 ) {
			#check only if interface is not excluded
			if (!defined $unused_names{$ifStatus{$key}{$snmpIfDescr}} ) {
				# check only if interface type is not listed in %excluded
				if (!defined $excluded{$ifStatus{$key}{$snmpIfType}} ) {
					if ($ifStatus{$key}{$snmpIfOperStatus} == 1 ) { $ifup++ ; }
					if ($ifStatus{$key}{$snmpIfOperStatus} == 2 ) {
									$ifdown++ ;
									if (defined $ifXTable) {
										$ifmessage .= sprintf("%s: down -> %s<BR>\n", $ifStatus{$key}{$snmpIfName}, $ifStatus{$key}{$snmpIfAlias});
									}else{
										$ifmessage .= sprintf("%s: down <BR>\n",$ifStatus{$key}{$snmpIfDescr});
									}
					}
					if ($ifStatus{$key}{$snmpIfOperStatus} == 5 ) { $ifdormant++ ;}
				} else {
					$ifexclude++;
				}
			} else {
				$ifunused++;
			}
		
		}
	}else{
		$ifunused++;
	}
}

   if ($ifdown > 0) {
      $state = 'CRITICAL';
      $answer = sprintf("host '%s', interfaces up: %d, down: %d, dormant: %d, excluded: %d, unused: %d<BR>",
                        $hostname,
			$ifup,
			$ifdown,
			$ifdormant,
			$ifexclude,
			$ifunused);
      $answer = $answer . $ifmessage . "\n";
   }
   else {
      $state = 'OK';
      $answer = sprintf("host '%s', interfaces up: %d, down: %d, dormant: %d, excluded: %d, unused: %d",
                        $hostname,
			$ifup,
			$ifdown,
			$ifdormant,
			$ifexclude,
			$ifunused);
   }
my $perfdata = sprintf("up=%d down=%d dormant=%d excluded=%d unused=%d",$ifup,$ifdown,$ifdormant,$ifexclude,$ifunused);
print ("$state: $answer |$perfdata\n");
exit $ERRORS{$state};

sub usage($) {
	print "$_[0]\n";
	print_usage();
	exit $ERRORS{"UNKNOWN"};
}

sub print_usage() {
	printf "\n";
	printf "usage: \n";
	printf "check_ifstatus -C <READCOMMUNITY> -p <PORT> -H <HOSTNAME>\n";
	printf "Copyright (C) 2000 Christoph Kron\n";
	printf "Updates 5/2002 Subhendu Ghosh\n";
	support();
	printf "\n\n";
}

sub print_help() {
	print_revision($PROGNAME, '@NP_VERSION@');
	print_usage();
	printf "check_ifstatus plugin for monitoring operational \n";
	printf "status of each network interface on the target host\n";
	printf "\nUsage:\n";
	printf "   -H (--hostname)   Hostname to query - (required)\n";
	printf "   -C (--community)  SNMP read community (defaults to public,\n";
	printf "                     used with SNMP v1 and v2c\n";
	printf "   -v (--snmp_version)  1 for SNMP v1 (default)\n";
	printf "                        2 for SNMP v2c\n";
	printf "                          SNMP v2c will use get_bulk for less overhead\n";
	printf "                        3 for SNMPv3 (requires -U option)";
	printf "   -p (--port)       SNMP port (default 161)\n";
	printf "   -I (--ifmib)      Agent supports IFMIB ifXTable.  For Cisco - this will provide\n";
	printf "                     the descriptive name.  Do not use if you don't know what this is. \n";
	printf "   -x (--exclude)    A comma separated list of ifType values that should be excluded \n";
	printf "                     from the report (default for an empty list is PPP(23).\n";
	printf "   -n (--unused_ports_by_name) A comma separated list of ifDescr values that should be excluded \n";
	printf "                     from the report (default is an empty exclusion list).\n";
	printf "   -u (--unused_ports) A comma separated list of ifIndex values that should be excluded \n";
	printf "                     from the report (default is an empty exclusion list).\n";
	printf "                     See the IANAifType-MIB for a list of interface types.\n";
	printf "   -L (--seclevel)   choice of \"noAuthNoPriv\", \"authNoPriv\", or	\"authPriv\"\n";
	printf "   -U (--secname)    username for SNMPv3 context\n";
	printf "   -c (--context)    SNMPv3 context name (default is empty string)\n";
	printf "   -A (--authpass)   authentication password (cleartext ascii or localized key\n";
	printf "                     in hex with 0x prefix generated by using \"snmpkey\" utility\n"; 
	printf "                     auth password and authEngineID\n";
	printf "   -a (--authproto)  Authentication protocol (MD5 or SHA1)\n";
	printf "   -X (--privpass)   privacy password (cleartext ascii or localized key\n";
	printf "                     in hex with 0x prefix generated by using \"snmpkey\" utility\n"; 
	printf "                     privacy password and authEngineID\n";
	printf "   -P (--privproto)  privacy protocol (DES or AES; default: DES)\n";
	printf "   -M (--maxmsgsize) Max message size - usefull only for v1 or v2c\n";
	printf "   -t (--timeout)    seconds before the plugin times out (default=$TIMEOUT)\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	print_revision($PROGNAME, '@NP_VERSION@');
	
}

sub process_arguments() {
	$status = GetOptions(
		"V"   => \$opt_V, "version"    => \$opt_V,
		"h"   => \$opt_h, "help"       => \$opt_h,
		"v=s" => \$snmp_version, "snmp_version=s"  => \$snmp_version,
		"C=s" => \$community,"community=s" => \$community,
		"L=s" => \$seclevel, "seclevel=s" => \$seclevel,
		"a=s" => \$authproto, "authproto=s" => \$authproto,
		"U=s" => \$secname,   "secname=s"   => \$secname,
		"A=s" => \$authpass,  "authpass=s"  => \$authpass,
		"X=s" => \$privpass,  "privpass=s"  => \$privpass,
		"P=s" => \$privproto,  "privproto=s"  => \$privproto,
		"c=s" => \$context,   "context=s"   => \$context,
		"p=i" =>\$port, "port=i" => \$port,
		"H=s" => \$hostname, "hostname=s" => \$hostname,
		"I"		=> \$ifXTable, "ifmib" => \$ifXTable,
		"x:s"		=>	\$opt_x,   "exclude:s" => \$opt_x,
		"u=s" => \$opt_u,  "unused_ports=s" => \$opt_u,
		"n=s" => \$opt_n, "unused_ports_by_name=s" => \$opt_n,
		"M=i" => \$maxmsgsize, "maxmsgsize=i" => \$maxmsgsize,
		"t=i" => \$timeout,    "timeout=i" => \$timeout,
		);
		
	if ($status == 0){
		print_help();
		exit $ERRORS{'UNKNOWN'};
	}

	if ($opt_V) {
		print_revision($PROGNAME,'@NP_VERSION@');
		exit $ERRORS{'UNKNOWN'};
	}

	if ($opt_h) {
		print_help();
		exit $ERRORS{'UNKNOWN'};
	}

	unless (defined $timeout) {
		$timeout = $TIMEOUT;
	}

	# Net::SNMP wants an integer
	$snmp_version = 2 if $snmp_version eq "2c";

	if ($snmp_version !~ /^[123]$/){
		$state='UNKNOWN';
		print ("$state: No support for SNMP v$snmp_version yet\n");
		exit $ERRORS{$state};
	}

	%session_opts = (
		-hostname   => $hostname,
		-port       => $port,
		-version    => $snmp_version,
		-maxmsgsize => $maxmsgsize
	);

	$session_opts{'-community'} = $community if (defined $community && $snmp_version =~ /[12]/);

	if ($snmp_version =~ /3/ ) {
		# Must define a security level even though default is noAuthNoPriv
		# v3 requires a security username
		if (defined $seclevel && defined $secname) {
			$session_opts{'-username'} = $secname;
		
			# Must define a security level even though defualt is noAuthNoPriv
			unless ( grep /^$seclevel$/, qw(noAuthNoPriv authNoPriv authPriv) ) {
				usage("Must define a valid security level even though default is noAuthNoPriv");
			}
			
			# Authentication wanted
			if ( $seclevel eq 'authNoPriv' || $seclevel eq 'authPriv' ) {
				if (defined $authproto && $authproto ne 'MD5' && $authproto ne 'SHA1') {
					usage("Auth protocol can be either MD5 or SHA1");
				}
				$session_opts{'-authprotocol'} = $authproto if(defined $authproto);

				if ( !defined $authpass) {
					usage("Auth password/key is not defined");
				}else{
					if ($authpass =~ /^0x/ ) {
						$session_opts{'-authkey'} = $authpass ;
					}else{
						$session_opts{'-authpassword'} = $authpass ;
					}
				}
			}
			
			# Privacy (DES encryption) wanted
			if ($seclevel eq 'authPriv' ) {
				if (! defined $privpass) {
					usage("Privacy passphrase/key is not defined");
				}else{
					if ($privpass =~ /^0x/){
						$session_opts{'-privkey'} = $privpass;
					}else{
						$session_opts{'-privpassword'} = $privpass;
					}
				}

				$session_opts{'-privprotocol'} = $privproto if(defined $privproto);
			}

			# Context name defined or default
			unless ( defined $context) {
				$context = "";
			}
		
		}else {
			usage("Security level or name is not defined");
		}
	} # end snmpv3

	# Excluded interfaces types (ifType) (backup interfaces, dial-on demand interfaces, PPP interfaces
	if (defined $opt_x) {
		my @x = split(/,/, $opt_x);
		if ( @x) {
			foreach $key (@x){
				$excluded{$key} = 1;
			}
		}else{
			$excluded{23} = 1; # default PPP(23) if empty list - note (AIX seems to think PPP is 22 according to a post)
		}
	}
	
	# Excluded interface descriptors
	if (defined $opt_n) {
		my @unused = split(/,/,$opt_n);
		if ( @unused ) {
			foreach $key (@unused) {
				$unused_names{$key} = 1;
			}
		}
	}

	# Excluded interface ports (ifIndex) - management reasons
	if ($opt_u) {
		@unused_ports = split(/,/,$opt_u);
		foreach $key (@unused_ports) { 
			$ifStatus{$key}{'notInUse'}++ ;
		}
	}

	if (! utils::is_hostname($hostname)){
		usage("Hostname invalid or not given");
		exit $ERRORS{"UNKNOWN"};
	}

		
	if ($snmp_version !~ /[123]/) {
		$state='UNKNOWN';
		print ("$state: No support for SNMP v$snmp_version yet\n");
		exit $ERRORS{$state};
	}

return $ERRORS{"OK"};
}
