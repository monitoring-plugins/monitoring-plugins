#! /usr/bin/perl -wT
#
# check_rpc plugin for nagios
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
# current status: $Revision$
#
# Copyright Notice: GPL
#


use strict;
use FindBin;
use lib "$FindBin::Bin";
use utils qw($TIMEOUT %ERRORS &print_revision &support);
use vars qw($PROGNAME);
my ($verbose,@proto,%prognum,$host,$response,$prognum,$port,$cmd);
my ($array_ref,$test,$element,@progkeys,$proto,$a,$b);
my ($opt_V,$opt_h,$opt_C,$opt_p,$opt_H);
$opt_V = $opt_h = $opt_C = $opt_p = $opt_H = '';

sub print_help ();
sub print_usage ();
sub in ($$);

$ENV{'BASH_ENV'}=''; 
$ENV{'ENV'}='';
$ENV{'PATH'}='';

#Initialise protocol for each progname number
# 'u' for UDP, 't' for TCP 
$proto[10003]='u';
$proto[10004]='u';
$proto[10007]='u';

use Getopt::Long;
Getopt::Long::Configure('bundling');
GetOptions
		("V"   => \$opt_V, "version"    => \$opt_V,
	   "h"   => \$opt_h, "help"       => \$opt_h,
	   "C=s" => \$opt_C, "command=s"  => \$opt_C,
	   "p=i" => \$opt_p, "port=i"     => \$opt_p,
	   "H=s" => \$opt_H, "hostname=s" => \$opt_H);

# -h means display verbose help screen
if ($opt_h) { print_help(); exit 0; }

# -V means display version number
if ($opt_V) { print_revision($PROGNAME,'$Revision$ '); exit 0; }

# -H means host name
$opt_H = shift unless ($opt_H);
unless ($opt_H) { print_usage(); exit -1; }
if($opt_H && $opt_H =~ m/^([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+|[a-zA-Z][-a-zA-Z0-9]+(\.[a-zA-Z][-a-zA-Z0-9]+)*)$/) {
    $host = $1;
} else {
    print "$opt_H is not a valid host name\n";
    exit -1;
}

while (<DATA>) {
	($a,$b) = split;
	$prognum{$a} = $b;
}
close DATA;

# -C means command name or number
$opt_C = shift unless ($opt_C);
unless ($opt_C) { print_usage(); exit -1; }
@progkeys = keys %prognum;
if ($opt_C =~ m/^([0-9]+)$/){
    $response = "RPC ok: program $opt_p (version ";
    $prognum = $1;
} elsif ( in( \@progkeys, $opt_C)) {
    $response = "RPC ok: $opt_C (version ";
    $prognum = $prognum{$opt_C};
} else {
    print "Program $opt_C is not defined\n";
    exit -1;
}

# -p means port number
if($opt_p =~ /^([0-9]+)$/){
    $port = "-n $1";
} else {
    $port = "";
}

$proto = 'u';
$proto = $proto[$prognum] if ($proto[$prognum]);
$cmd = "/usr/sbin/rpcinfo $port -" . "$proto $host $prognum 2>&1 |";
print "$cmd\n" if ($verbose);
open CMD, $cmd;

while ( <CMD> ) {
    chomp;
    if ( /program $prognum version ([0-9]*) ready and waiting/ ) {
	$response .= "$1) is running";
        print "$response\n";
        exit 0;
    }
}

print "RPC CRITICAL: Program $opt_C not registered\n";
exit 2;



sub print_help() {
	print_revision($PROGNAME,'$Revision$ ');
	print "Copyright (c) 2000 Karl DeBisschop/Truongchinh Nguyen\n";
	print "\n";
	print "Check if a rpc service is registered and running using\n";
	print "      rpcinfo -<protocol> <host> <program number>\n";
	print "\n";
	print_usage();
	print "\n";
	print "<host>    The server providing the rpc service\n";
	print "<program> The program name (or number).\n\n";
	support();
}

sub print_usage () {
	print "$PROGNAME -H host -C rpc_command [-p port]\n";
	print "$PROGNAME [-h | --help]\n";
	print "$PROGNAME [-V | --version]\n";
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

__DATA__
portmapper 100000
portmap 100000
sunrpc 100000
rpcbind 100000
rstatd 100001
rstat 100001
rup 100001
perfmeter 100001
rstat_svc 100001
rusersd 100002
rusers 100002
nfs 100003
nfsprog 100003
ypserv 100004
ypprog 100004
mountd 100005
mount 100005
showmount 100005
ypbind 100007
walld 100008
rwall 100008
shutdown 100008
yppasswdd 100009
yppasswd 100009
etherstatd 100010
etherstat 100010
rquotad 100011
rquotaprog 100011
quota 100011
rquota 100011 
sprayd 100012
spray 100012
3270_mapper 100013
rje_mapper 100014
selection_svc 100015
selnsvc 100015
database_svc 100016
rexd 100017
rex 100017
alis 100018
sched 100019
llockmgr 100020
nlockmgr 100021
x25_inr 100022
statmon 100023
status 100024
bootparam 100026
ypupdated 100028
ypupdate 100028
keyserv 100029
keyserver 100029 
sunlink_mapper 100033
tfsd 100037
nsed 100038
nsemntd 100039
showfhd 100043
showfh 100043
ioadmd 100055
rpc.ioadmd 100055
NETlicense 100062
sunisamd 100065
debug_svc 100066
dbsrv 100066
ypxfrd 100069
rpc.ypxfrd 100069
bugtraqd 100071
kerbd 100078
event 100101
na.event 100101
logger 100102
na.logger 100102
sync 100104
na.sync 100104
hostperf 100107
na.hostperf 100107
activity 100109
na.activity 100109
hostmem 100112
na.hostmem 100112
sample 100113
na.sample 100113
x25 100114
na.x25 100114
ping 100115
na.ping 100115
rpcnfs 100116
na.rpcnfs 100116
hostif 100117
na.hostif 100117
etherif 100118
na.etherif 100118
iproutes 100120
na.iproutes 100120
layers 100121
na.layers 100121
snmp 100122
na.snmp 100122
snmp-cmc 100122
snmp-synoptics 100122
snmp-unisys 100122
snmp-utk 100122
traffic 100123
na.traffic 100123
nfs_acl 100227
sadmind 100232
nisd 100300
rpc.nisd 100300
nispasswd 100303
rpc.nispasswdd 100303
ufsd 100233
ufsd 100233
pcnfsd 150001
pcnfs 150001
amd 300019
amq 300019
bwnfsd 545580417
fypxfrd 600100069
freebsd-ypxfrd 600100069
