#!/usr/local/bin/perl
#
# check_nfs plugin for nagios
#
# usage:
#    check_nfs.pl server
#
# Check if a nfs server is registered and running
# using rpcinfo -T udp <arg1> 100003.
# 100003 is the rpc programmnumber for nfs.
# <arg1> is the server queried.
#
#
# Use these hosts.cfg entries as examples
#
#service[fs0]=NFS;24x7;3;5;5;unix-admin;60;24x7;1;1;1;;check_nfs
#command[check_nfs]=/some/path/libexec/check_nfs.pl $HOSTADDRESS$
#
# initial version: 9-13-99 Ernst-Dieter Martin edmt@infineon.com
# current status: looks like working
#
#
# Copyright Notice: Do as you please, credit me, but don't blame me
#


$server = shift;


open CMD,"/bin/rpcinfo -T udp $server 100003 |";

$response = "nfs version ";

while ( <CMD> ) {
  if ( /program 100003 version ([0-9]*) ready and waiting/ ) {
	$response = $ response . "$1,";
  }
}

if ( $response eq "nfs version " ) {
  print "rpcinfo: RPC: Program not registered\n";
  exit 2;
}

$response =~ s/,$//;
print "$response\n";

exit 0;
