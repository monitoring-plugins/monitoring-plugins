#! /usr/bin/perl -w -I ..
#
# Internet Mail Access Protocol (IMAP) Server Tests via check_imap
#
#

use strict;
use Test::More tests => 7;
use NPTest;

my $host_tcp_smtp      = getTestParameter("NP_HOST_TCP_SMTP", "A host providing an STMP Service (a mail server)", "mailhost");
my $host_tcp_imap      = getTestParameter("NP_HOST_TCP_IMAP", "A host providing an IMAP Service (a mail server)", $host_tcp_smtp);
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my $t;

$t = NPTest->testCmd( "./check_imap $host_tcp_imap" );
cmp_ok( $t->return_code, '==', 0, "Contacted imap" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -p 143 -w 9 -c 9 -to 10 -e '* OK'" );
cmp_ok( $t->return_code, '==', 0, "Got right response" );

$t = NPTest->testCmd( "./check_imap $host_tcp_imap -p 143 -wt 9 -ct 9 -to 10 -e '* OK'" );
cmp_ok( $t->return_code, '==', 0, "Check old parameter options" );

$t = NPTest->testCmd( "./check_imap $host_nonresponsive" );
cmp_ok( $t->return_code, '==', 2, "Get error with non responsive host" );

$t = NPTest->testCmd( "./check_imap $hostname_invalid" );
cmp_ok( $t->return_code, '==', 2, "Invalid hostname" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -e unlikely_string");
cmp_ok( $t->return_code, '==', 1, "Got warning with bad response" );

$t = NPTest->testCmd( "./check_imap -H $host_tcp_imap -e unlikely_string -M crit");
cmp_ok( $t->return_code, '==', 2, "Got critical error with bad response" );

