#!@PERL@ -w
#
# check_ifoperstatus.pl - monitoring plugin
#
# Copyright (C) 2000 Christoph Kron,
# Modified 5/2002 to conform to updated Monitoring Plugins Guidelines
# Added support for named interfaces per Valdimir Ivaschenko (S. Ghosh)
# Added SNMPv3 support (10/2003)
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
# Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
# USA
#
#
# Report bugs to:  help@monitoring-plugins.org
#
# 11.01.2000 Version 1.0
#
# Patches from Guy Van Den Bergh to warn on ifadminstatus down interfaces
# instead of critical.
#
# Primary MIB reference - RFC 2863


use POSIX;
use strict;
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);

use Net::SNMP;
use Getopt::Long;
&Getopt::Long::config('bundling');

my $PROGNAME = "check_ifoperstatus";
sub print_help ();
sub usage ($);
sub print_usage ();
sub process_arguments ();

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';

my $timeout;
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
my $maxmsgsize = 1472 ; # Net::SNMP default is 1472
my ($seclevel, $authproto, $secname, $authpass, $privpass, $privproto, $auth, $priv, $context);
my $port = 161;
my @snmpoids;
my $sysUptime        = '1.3.6.1.2.1.1.3.0';
my $snmpIfDescr      = '1.3.6.1.2.1.2.2.1.2';
my $snmpIfType       = '1.3.6.1.2.1.2.2.1.3';
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
my $iftype;
my $key;
my $lastc;
my $dormantWarn;
my $adminWarn;
my $name;
my %session_opts;

### Validate Arguments

$status = process_arguments();


# Just in case of problems, let's not hang the monitoring system
$SIG{'ALRM'} = sub {
	print ("ERROR: No snmp response from $hostname (alarm)\n");
	exit $ERRORS{"UNKNOWN"};
};

alarm($timeout);

($session, $error) = Net::SNMP->session(%session_opts);

		
if (!defined($session)) {
			$state='UNKNOWN';
			$answer=$error;
			print ("$state: $answer\n");
			exit $ERRORS{$state};
}

## map ifdescr to ifindex - should look at being able to cache this value

if (defined $ifdescr || defined $iftype) {
	# escape "/" in ifdescr - very common in the Cisco world
	if (defined $iftype) {
		$status=fetch_ifindex($snmpIfType, $iftype);
	} else {
		$ifdescr =~ s/\//\\\//g;
		$status=fetch_ifindex($snmpIfDescr, $ifdescr);  # if using on device with large number of interfaces
		                                                # recommend use of SNMP v2 (get-bulk)
	}
	if ($status==0) {
		$state = "UNKNOWN";
		printf "$state: could not retrive ifdescr/iftype snmpkey - $status-$snmpkey\n";
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
if ( defined $ifName && not ($response->{$snmpIfName} eq $ifName) ) {
	$state = 'UNKNOWN';
	$answer = "Interface name ($ifName) doesn't match snmp value ($response->{$snmpIfName}) (index $snmpkey)";
	print ("$state: $answer\n");
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
	$answer = "Interface $name (index $snmpkey) is administratively down.";
	if ( not defined $adminWarn or $adminWarn eq "w" ) {
		$state = 'WARNING';
	} elsif ( $adminWarn eq "i" ) {
		$state = 'OK';
	} elsif ( $adminWarn eq "c" ) {
		$state = 'CRITICAL';
	} else { # If wrong value for -a, say warning
		$state = 'WARNING';
	}
} 
## Check operational status
elsif ( $response->{$snmpIfOperStatus} == 2 ) {
	$state = 'CRITICAL';
	$answer = "Interface $name (index $snmpkey) is down.";
} elsif ( $response->{$snmpIfOperStatus} == 5 ) {
	if (defined $dormantWarn ) {
		if ($dormantWarn eq "w") {
			$state = 'WARNING';
			$answer = "Interface $name (index $snmpkey) is dormant.";
		}elsif($dormantWarn eq "c") {
			$state = 'CRITICAL';
			$answer = "Interface $name (index $snmpkey) is dormant.";
		}elsif($dormantWarn eq "i") {
			$state = 'OK';
			$answer = "Interface $name (index $snmpkey) is dormant.";
		}
	}else{
		# dormant interface - but warning/critical/ignore not requested
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



print ("$state: $answer\n");
exit $ERRORS{$state};


### subroutines

sub fetch_ifindex {
	my $oid = shift;
	my $lookup = shift;

	if (!defined ($response = $session->get_table($oid))) {
		$answer=$session->error;
		$session->close;
		$state = 'CRITICAL';
		printf ("$state: SNMP error with snmp version $snmp_version ($answer)\n");
		$session->close;
		exit $ERRORS{$state};
	}
	
	foreach $key ( keys %{$response}) {
		if ($response->{$key} =~ /^$lookup$/) {
			$key =~ /.*\.(\d+)$/;
			$snmpkey = $1;
			#print "$lookup = $key / $snmpkey \n";  #debug
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

sub usage($) {
	print "$_[0]\n";
	print_usage();
	exit $ERRORS{"UNKNOWN"};
}

sub print_usage() {
	printf "\n";
	printf "usage: \n";
	printf "check_ifoperstatus -k <IF_KEY> -H <HOSTNAME> [-C <community>]\n";
	printf "Copyright (C) 2000 Christoph Kron\n";
	printf "check_ifoperstatus.pl comes with ABSOLUTELY NO WARRANTY\n";
	printf "This programm is licensed under the terms of the ";
	printf "GNU General Public License\n(check source code for details)\n";
	printf "\n\n";
}

sub print_help() {
	print_revision($PROGNAME, '@NP_VERSION@');
	print_usage();
	printf "check_ifoperstatus plugin for monitoring operational \n";
	printf "status of a particular network interface on the target host\n";
	printf "\nUsage:\n";
	printf "   -H (--hostname)   Hostname to query - (required)\n";
	printf "   -C (--community)  SNMP read community (defaults to public,\n";
	printf "                     used with SNMP v1 and v2c\n";
	printf "   -v (--snmp_version)  1 for SNMP v1 (default)\n";
	printf "                        2 for SNMP v2c\n";
	printf "                        SNMP v2c will use get_bulk for less overhead\n";
	printf "                        if monitoring with -d\n";
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
	printf "   -k (--key)        SNMP IfIndex value\n";
	printf "   -d (--descr)      SNMP ifDescr value\n";
	printf "   -T (--type)       SNMP ifType integer value (see http://www.iana.org/assignments/ianaiftype-mib)\n";
	printf "   -p (--port)       SNMP port (default 161)\n";
	printf "   -I (--ifmib)      Agent supports IFMIB ifXTable. Do not use if\n";
	printf "                     you don't know what this is. \n";
	printf "   -n (--name)       the value should match the returned ifName\n";
	printf "                     (Implies the use of -I)\n";
	printf "   -w (--warn =i|w|c) ignore|warn|crit if the interface is dormant (default critical)\n";
	printf "   -D (--admin-down =i|w|c) same for administratively down interfaces (default warning)\n";
	printf "   -M (--maxmsgsize) Max message size - usefull only for v1 or v2c\n";
	printf "   -t (--timeout)    seconds before the plugin times out (default=$TIMEOUT)\n";
	printf "   -V (--version)    Plugin version\n";
	printf "   -h (--help)       usage help \n\n";
	printf " -k or -d or -T must be specified\n\n";
	printf "Note: either -k or -d or -T must be specified and -d and -T are much more network \n";
	printf "intensive.  Use it sparingly or not at all.  -n is used to match against\n";
	printf "a much more descriptive ifName value in the IfXTable to verify that the\n";
	printf "snmpkey has not changed to some other network interface after a reboot.\n\n";
	
}

sub process_arguments() {
	$status = GetOptions(
			"V"   => \$opt_V, "version"    => \$opt_V,
			"h"   => \$opt_h, "help"       => \$opt_h,
			"v=i" => \$snmp_version, "snmp_version=i"  => \$snmp_version,
			"C=s" => \$community, "community=s" => \$community,
			"L=s" => \$seclevel, "seclevel=s" => \$seclevel,
			"a=s" => \$authproto, "authproto=s" => \$authproto,
			"U=s" => \$secname,   "secname=s"   => \$secname,
			"A=s" => \$authpass,  "authpass=s"  => \$authpass,
			"X=s" => \$privpass,  "privpass=s"  => \$privpass,
			"P=s" => \$privproto,  "privproto=s"  => \$privproto,
			"c=s" => \$context,   "context=s"   => \$context,
			"k=i" => \$snmpkey, "key=i",\$snmpkey,
			"d=s" => \$ifdescr, "descr=s" => \$ifdescr,
			"l=s" => \$lastc,  "lastchange=s" => \$lastc,
			"p=i" => \$port,  "port=i" =>\$port,
			"H=s" => \$hostname, "hostname=s" => \$hostname,
			"I"   => \$ifXTable, "ifmib" => \$ifXTable,
			"n=s" => \$ifName, "name=s" => \$ifName,
			"w=s" => \$dormantWarn, "warn=s" => \$dormantWarn,
			"D=s" => \$adminWarn, "admin-down=s" => \$adminWarn,
			"M=i" => \$maxmsgsize, "maxmsgsize=i" => \$maxmsgsize,
			"t=i" => \$timeout,    "timeout=i" => \$timeout,
			"T=i" => \$iftype,    "type=i" => \$iftype,
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

	if (! utils::is_hostname($hostname)){
		usage("Hostname invalid or not given");
	}

	unless ($snmpkey > 0 || defined $ifdescr || defined $iftype){
		usage("Either a valid snmp key (-k) or a ifDescr (-d) must be provided");
	}

	if (defined $ifName) {
		$ifXTable=1;
	}	

	if (defined $dormantWarn) {
		unless ($dormantWarn =~ /^(w|c|i)$/ ) {
			printf "Dormant alerts must be one of w|c|i \n";
			exit $ERRORS{'UNKNOWN'};
		}
	}
	
	unless (defined $timeout) {
		$timeout = $TIMEOUT;
	}

	if ($snmp_version !~ /[123]/){
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


}
## End validation

