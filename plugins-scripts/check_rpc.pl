#!@PERL@ -w
#
# check_rpc plugin for monitoring
#
# usage:
#    check_rpc host service
#
# Check if an rpc serice is registered and running
# using rpcinfo - $proto $host $prognum 2>&1 |";
#
# Use these hosts.cfg entries as examples
#
# command[check_nfs]=/some/path/libexec/check_rpc $HOSTADDRESS$ nfs
# service[check_nfs]=NFS;24x7;3;5;5;unix-admin;60;24x7;1;1;1;;check_rpc
#
# initial version: 3 May 2000 by Truongchinh Nguyen and Karl DeBisschop
# Modified May 2002 Subhendu Ghosh - support for ePN and patches
#
# Copyright Notice: GPL
#

use strict;
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);
use vars qw($PROGNAME);
my ($verbose,@proto,%prognum,$host,$response,$prognum,$port,$cmd,$progver,$state);
my ($array_ref,$test,$element,@progkeys,$proto,$a,$b);
my ($opt_V,$opt_h,$opt_C,$opt_p,$opt_H,$opt_c,$opt_u,$opt_t);
my ($line, @progvers, $response2,$response3);
$opt_V = $opt_h = $opt_C = $opt_p = $opt_H =  $opt_u = $opt_t ='';
$state = 'UNKNOWN';
$progver = $response=$response2= $response3 ='';

$PROGNAME = "check_rpc";
sub print_help ();
sub print_usage ();
sub in ($$);

$ENV{'PATH'}='@TRUSTED_PATH@';
$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';
$ENV{'LC_ALL'}='C';

#Initialise protocol for each progname number
# 'u' for UDP, 't' for TCP
$proto[10003]='u';
$proto[10004]='u';
$proto[10007]='u';

use Getopt::Long;
Getopt::Long::Configure('bundling');
GetOptions(
	"V"   => \$opt_V,   "version"    => \$opt_V,
	"h"   => \$opt_h,   "help"       => \$opt_h,
	"C=s" => \$opt_C,   "command=s"  => \$opt_C,
	"p=i" => \$opt_p,   "port=i"     => \$opt_p,
 	"H=s" => \$opt_H,   "hostname=s" => \$opt_H,
 	"c=s" => \$opt_c,   "progver=s"  => \$opt_c,
 	"v+"  => \$verbose, "verbose+"   => \$verbose,
 	"u"   => \$opt_u,   "udp"        => \$opt_u,
 	"t"   => \$opt_t,   "tcp"        => \$opt_t
);

# -h means display verbose help screen
if ($opt_h) { print_help(); exit $ERRORS{'UNKNOWN'}; }

# -V means display version number
if ($opt_V) {
	print_revision($PROGNAME,'@NP_VERSION@');
	exit $ERRORS{'UNKNOWN'};
}

# Hash containing all RPC program names and numbers
# Add to the hash if support for new RPC program is required

%prognum  = (
	"portmapper"  => 100000 ,
	"portmap"  => 100000 ,
	"sunrpc"  => 100000 ,
	"rpcbind"  => 100000 ,
	"rstatd"  => 100001 ,
	"rstat"  => 100001 ,
	"rup"  => 100001 ,
	"perfmeter"  => 100001 ,
	"rstat_svc"  => 100001 ,
	"rusersd"  => 100002 ,
	"rusers"  => 100002 ,
	"nfs"  => 100003 ,
	"nfsprog"  => 100003 ,
	"ypserv"  => 100004 ,
	"ypprog"  => 100004 ,
	"mountd"  => 100005 ,
	"mount"  => 100005 ,
	"showmount"  => 100005 ,
	"ypbind"  => 100007 ,
	"walld"  => 100008 ,
	"rwall"  => 100008 ,
	"shutdown"  => 100008 ,
	"yppasswdd"  => 100009 ,
	"yppasswd"  => 100009 ,
	"etherstatd"  => 100010 ,
	"etherstat"  => 100010 ,
	"rquotad"  => 100011 ,
	"rquotaprog"  => 100011 ,
	"quota"  => 100011 ,
	"rquota"  => 100011 ,
	"sprayd"  => 100012 ,
	"spray"  => 100012 ,
	"3270_mapper"  => 100013 ,
	"rje_mapper"  => 100014 ,
	"selection_svc"  => 100015 ,
	"selnsvc"  => 100015 ,
	"database_svc"  => 100016 ,
	"rexd"  => 100017 ,
	"rex"  => 100017 ,
	"alis"  => 100018 ,
	"sched"  => 100019 ,
	"llockmgr"  => 100020 ,
	"nlockmgr"  => 100021 ,
	"x25_inr"  => 100022 ,
	"statmon"  => 100023 ,
	"status"  => 100024 ,
	"bootparam"  => 100026 ,
	"ypupdated"  => 100028 ,
	"ypupdate"  => 100028 ,
	"keyserv"  => 100029 ,
	"keyserver"  => 100029 ,
	"sunlink_mapper"  => 100033 ,
	"tfsd"  => 100037 ,
	"nsed"  => 100038 ,
	"nsemntd"  => 100039 ,
	"showfhd"  => 100043 ,
	"showfh"  => 100043 ,
	"ioadmd"  => 100055 ,
	"rpc.ioadmd"  => 100055 ,
	"NETlicense"  => 100062 ,
	"sunisamd"  => 100065 ,
	"debug_svc"  => 100066 ,
	"dbsrv"  => 100066 ,
	"ypxfrd"  => 100069 ,
	"rpc.ypxfrd"  => 100069 ,
	"bugtraqd"  => 100071 ,
	"kerbd"  => 100078 ,
	"event"  => 100101 ,
	"na.event"  => 100101 ,
	"logger"  => 100102 ,
	"na.logger"  => 100102 ,
	"sync"  => 100104 ,
	"na.sync"  => 100104 ,
	"hostperf"  => 100107 ,
	"na.hostperf"  => 100107 ,
	"activity"  => 100109 ,
	"na.activity"  => 100109 ,
	"hostmem"  => 100112 ,
	"na.hostmem"  => 100112 ,
	"sample"  => 100113 ,
	"na.sample"  => 100113 ,
	"x25"  => 100114 ,
	"na.x25"  => 100114 ,
	"ping"  => 100115 ,
	"na.ping"  => 100115 ,
	"rpcnfs"  => 100116 ,
	"na.rpcnfs"  => 100116 ,
	"hostif"  => 100117 ,
	"na.hostif"  => 100117 ,
	"etherif"  => 100118 ,
	"na.etherif"  => 100118 ,
	"iproutes"  => 100120 ,
	"na.iproutes"  => 100120 ,
	"layers"  => 100121 ,
	"na.layers"  => 100121 ,
	"snmp"  => 100122 ,
	"na.snmp"  => 100122 ,
	"snmp-cmc"  => 100122 ,
	"snmp-synoptics"  => 100122 ,
	"snmp-unisys"  => 100122 ,
	"snmp-utk"  => 100122 ,
	"traffic"  => 100123 ,
	"na.traffic"  => 100123 ,
	"nfs_acl"  => 100227 ,
	"sadmind"  => 100232 ,
	"nisd"  => 100300 ,
	"rpc.nisd"  => 100300 ,
	"nispasswd"  => 100303 ,
	"rpc.nispasswdd"  => 100303 ,
	"ufsd"  => 100233 ,
	"ufsd"  => 100233 ,
	"pcnfsd"  => 150001 ,
	"pcnfs"  => 150001 ,
	"amd"  => 300019 ,
	"amq"  => 300019 ,
	"bwnfsd"  => 545580417 ,
	"fypxfrd"  => 600100069 ,
	"freebsd-ypxfrd"  => 600100069 ,
);

# -v means verbose, -v-v means verbose twice = print above hash
if (defined $verbose && ($verbose > 1) ){
	my $key;
	print "Supported programs:\n";
	print "    name\t=>\tnumber\n";
	print " ===============================\n";
	foreach $key (sort keys %prognum) {
		print "   $key \t=>\t$prognum{$key} \n";
	}
	print "\n\n";
	print_usage();
	exit $ERRORS{'OK'};
}

# -H means host name
unless ($opt_H) { print_usage(); exit $ERRORS{'UNKNOWN'}; }

if (! utils::is_hostname($opt_H)){
	print "$opt_H is not a valid host name\n";
	print_usage();
	exit $ERRORS{"UNKNOWN"};
}else{
	$host = $opt_H;
}

if ($opt_t && $opt_u) {
	print "Cannot define tcp AND udp\n";
	print_usage();
	exit $ERRORS{'UNKNOWN'};
}


# -C means command name or number
$opt_C = shift unless ($opt_C);
unless ($opt_C) { print_usage(); exit -1; }
@progkeys = keys %prognum;
if ($opt_C =~ m/^([0-9]+)$/){
#    $response = "RPC ok: program $opt_C (version ";
    $prognum = $1;
} elsif ( in( \@progkeys, $opt_C)) {
#    $response = "RPC ok: $opt_C (version ";
    $prognum = $prognum{$opt_C};
} else {
    print "Program $opt_C is not defined\n";
    exit $ERRORS{'UNKNOWN'};
}

# -p means port number
if($opt_p =~ /^([0-9]+)$/){
    $port = "-n $1";
} else {
    $port = "";
}

$proto = 'u';
$proto = $proto[$prognum] if ($proto[$prognum]);
$proto = 't' if ($opt_t);
$proto = 'u' if ($opt_u);


# Just in case of problems, let's not hang the monitoring system
$SIG{'ALRM'} = sub {
        print ("ERROR: No response from RPC server (alarm)\n");
        exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);

# -c is progver  - if we need to check multiple specified versions.
if (defined $opt_c ) {
	my $vers;
	@progvers = split(/,/ ,$opt_c );
	foreach $vers (sort @progvers) {
		if($vers =~ /^([0-9]+)$/){
			$progver = "$1";
			print "Checking $opt_C version $progver proto $proto\n" if $verbose;
			get_rpcinfo();
		}else{
			print "Version $vers is not an integer\n" if $verbose;
		}

	}
}else{
	get_rpcinfo();
}


## translate proto for output
if ($proto eq "u" ){
	$proto = "udp";
}else{
	$proto = "tcp";
}

if ($state eq 'OK') {
	print "$state: RPC program $opt_C".$response." $proto running\n";
}else{
	if($response){
		print "$state: RPC program $opt_C".$response2." $proto is not running,".$response." $proto is running\n";
	}else{
		print "$state: RPC program $opt_C $response2 $proto is not running\n";
	}
}
exit $ERRORS{$state};


########  Subroutines ==========================

sub get_rpcinfo {
	$cmd = "$utils::PATH_TO_RPCINFO $port -" . "$proto $host $prognum $progver 2>&1 |";
	print "$cmd\n" if ($verbose);
	open CMD, $cmd or die "Can't fork for rpcinfo: $!\n" ;

	while ( $line = <CMD> ) {
		printf "$line " if $verbose;
		chomp $line;

    	if ( $line =~ /program $prognum version ([0-9]*) ready and waiting/ ) {
			$response .= " version $1";
			$state = 'OK' unless $state ne 'UNKNOWN';
			print "1:$response \n" if $verbose;
    	}

		if ( $line =~ /program $prognum version ([0-9]*) is not available/ ) {
			$response2 .= " version $1";
			$state = 'CRITICAL';
			print "2:$response2 \n" if $verbose;
		}
		if ( $line =~ /program $prognum is not available/ ) {
			$response3 = "";
			$response3 = "tcp" if $opt_t;
			$response3 = "udp" if $opt_u;
			$state = 'CRITICAL';
			print "3:$response3 \n" if $verbose;
		}
	}
	close CMD;
}


sub print_help() {
	print_revision($PROGNAME,'@NP_VERSION@');
	print "Copyright (c) 2002 Karl DeBisschop/Truongchinh Nguyen/Subhendu Ghosh\n";
	print "\n";
	print "Check if a rpc service is registered and running using\n";
	print "      rpcinfo -H host -C rpc_command \n";
	print "\n";
	print_usage();
	print "\n";
	print "  <host>          The server providing the rpc service\n";
	print "  <rpc_command>   The program name (or number).\n";
	print "  <program_version> The version you want to check for (one or more)\n";
	print "                    Should prevent checks of unknown versions being syslogged\n";
	print "                    e.g. 2,3,6 to check v2, v3, and v6\n";
	print "  [-u | -t]       Test UDP or TCP\n";
	print "  [-v]            Verbose \n";
	print "  [-v -v]         Verbose - will print supported programs and numbers \n";
	print "\n";
	support();
}

sub print_usage () {
	print "Usage: \n";
	print " $PROGNAME -H host -C rpc_command [-p port] [-c program_version] [-u|-t] [-v]\n";
	print " $PROGNAME [-h | --help]\n";
	print " $PROGNAME [-V | --version]\n";
}

sub in ($$) {
	$array_ref = shift;
	$test = shift;

	while ( $element = shift @{$array_ref} ) {
		if ($test eq $element) {
	    return 1;
		}
	}
	return 0;
}

