#! /usr/bin/perl -w -I ..
#
# Domain Name Server (DNS) Tests via check_dns
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 6; plan tests => $tests}

my $successOutput = '/DNS OK: [\.0-9]+ seconds response time/';

my $hostname_valid   = getTestParameter( "hostname_valid",   "NP_HOSTNAME_VALID",   "localhost",
					 "A valid (known to DNS) hostname" );

my $hostname_invalid = getTestParameter( "hostname_invalid", "NP_HOSTNAME_INVALID", "nosuchhost",
					 "An invalid (not known to DNS) hostname" );

my $dns_server       = getTestParameter( "dns_server",       "NP_DNS_SERVER",       undef,
					 "A non default (remote) DNS server" );

my $t;

#
# Default DNS Server
#
$t += checkCmd( "./check_dns -H $hostname_valid   -t 5", 0, $successOutput );
$t += checkCmd( "./check_dns -H $hostname_invalid -t 1", 2 );

#
# Specified DNS Server
#
$t += checkCmd( "./check_dns -H $hostname_valid   -s $dns_server -t 5", 0, $successOutput );
$t += checkCmd( "./check_dns -H $hostname_invalid -s $dns_server -t 1", 2 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

