#! /usr/bin/perl -w -I ..
#
# Simple Network Management Protocol (SNMP) Test via check_snmp
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 12; plan tests => $tests}

my $t;

if ( -x "./check_snmp" )
{
  my $host_snmp          = getTestParameter( "host_snmp",          "NP_HOST_SNMP",      "localhost",
					     "A host providing an SNMP Service");

  my $snmp_community     = getTestParameter( "snmp_community",     "NP_SNMP_COMMUNITY", "public",
					     "The SNMP Community string for SNMP Testing" );

  my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					     "The hostname of system not responsive to network requests" );

  my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
					     "An invalid (not known to DNS) hostname" );

  my %exceptions = ( 3 => "No SNMP Server present?" );


  $t += checkCmd( "./check_snmp -H $host_snmp -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:",
		  { 0 => 'continue',  3 => 'skip' }, '/^SNMP OK - \d+/',		%exceptions );

  $t += checkCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 1:1 -c 1:1",
		  { 0 => 'continue',  3 => 'skip' }, '/^SNMP OK - 1\s.*$/',		%exceptions );

  $t += checkCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w 0   -c 1:",
		  { 1 => 'continue',  3 => 'skip' }, '/^SNMP WARNING - \*1\*\s.*$/',	%exceptions );

  $t += checkCmd( "./check_snmp -H $host_snmp -C $snmp_community -o host.hrSWRun.hrSWRunTable.hrSWRunEntry.hrSWRunIndex.1 -w  :0 -c 0",
		  { 2 => 'continue',  3 => 'skip' }, '/^SNMP CRITICAL - \*1\*\s.*$/',	%exceptions );

  $t += checkCmd( "./check_snmp -H $host_nonresponsive -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:", 3, '/SNMP problem - /' );

  $t += checkCmd( "./check_snmp -H $hostname_invalid   -C $snmp_community -o system.sysUpTime.0 -w 1: -c 1:", 3, '/SNMP problem - /' );

}
else
{
  $t += skipMissingCmd( "./check_snmp", $tests );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
