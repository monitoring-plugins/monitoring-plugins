#! /usr/bin/perl -w -I ..
#
# Internet Mail Access Protocol (IMAP) Server Tests via check_imap
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 7; plan tests => $tests}

my $host_tcp_smtp      = getTestParameter( "host_tcp_smtp",      "NP_HOST_TCP_SMTP",      "mailhost",
					   "A host providing an STMP Service (a mail server)");

my $host_tcp_imap      = getTestParameter( "host_tcp_imap",      "NP_HOST_TCP_IMAP",      $host_tcp_smtp,
					   "A host providing an IMAP Service (a mail server)");

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my %exceptions = ( 2 => "No IMAP Server present?" );

my $t;

$t += checkCmd( "./check_imap    $host_tcp_imap",                                     0, undef, %exceptions );
$t += checkCmd( "./check_imap -H $host_tcp_imap -p 143 -w  9 -c  9 -t  10 -e '* OK'", 0, undef, %exceptions );
$t += checkCmd( "./check_imap    $host_tcp_imap -p 143 -wt 9 -ct 9 -to 10 -e '* OK'", 0, undef, %exceptions );
$t += checkCmd( "./check_imap    $host_nonresponsive", 2 );
$t += checkCmd( "./check_imap    $hostname_invalid",   2 );
$t += checkCmd( "./check_imap -H $host_tcp_imap -e unlikely_string",                  1);
$t += checkCmd( "./check_imap -H $host_tcp_imap -e unlikely_string -M crit",          2);


exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
