#! /usr/bin/perl -w -I ..
#
# Internet Mail Access Protocol (IMAP) Server Tests via check_imap
#
#

use strict;
use Test::More tests => 7;
use NPTest;

my $host_tcp_smtp      = getTestParameter( "host_tcp_smtp",      "NP_HOST_TCP_SMTP",      "mailhost",
					   "A host providing an STMP Service (a mail server)");

my $host_tcp_imap      = getTestParameter( "host_tcp_imap",      "NP_HOST_TCP_IMAP",      $host_tcp_smtp,
					   "A host providing an IMAP Service (a mail server)");

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $t;

$t = NPTest->testCmd( "./check_imap $host_tcp_imap" );
cmp_ok( $t->return_code, '==', 0, "Contacted imap" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -p 143 -w 9 -c 9 -to 10 -e '* OK'" );
cmp_ok( $t->return_code, '==', 0, "Got right response" );

$t = NPTest->testCmd( "./check_imap $host_tcp_imap -p 143 -wt 9 -ct 9 -to 10 -e '* OK'" );
cmp_ok( $t->return_code, '==', 0, "Check old parameter options" );

$t = NPTest->testCmd( "./check_imap $host_nonresponsive" );
cmp_ok( $t->return_code, '==', 2, "Get error with non reponsive host" );

$t = NPTest->testCmd( "./check_imap $hostname_invalid" );
cmp_ok( $t->return_code, '==', 2, "Invalid hostname" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -e unlikely_string");
cmp_ok( $t->return_code, '==', 1, "Got warning with bad response" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -e unlikely_string -M crit");
cmp_ok( $t->return_code, '==', 2, "Got critical error with bad response" );

