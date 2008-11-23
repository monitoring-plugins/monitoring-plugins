#! /usr/bin/perl -w -I ..
#
# File Transfer Protocol (FTP) Test via check_ftp
#
#

use strict;
use Test;
use NPTest;

use vars qw($tests);
BEGIN {$tests = 4; plan tests => $tests}

my $host_tcp_ftp       = getTestParameter( "host_tcp_ftp",       "NP_HOST_TCP_FTP",       "localhost",
					   "A host providing the FTP Service (an FTP server)");

my $host_nonresponsive = getTestParameter( "host_nonresponsive", "NP_HOST_NONRESPONSIVE", "10.0.0.1",
					   "The hostname of system not responsive to network requests" );

my $hostname_invalid   = getTestParameter( "hostname_invalid",   "NP_HOSTNAME_INVALID",   "nosuchhost",
                                           "An invalid (not known to DNS) hostname" );

my $successOutput = '/FTP OK -\s+[0-9]?\.?[0-9]+ second response time/';

my $t;

$t += checkCmd( "./check_ftp $host_tcp_ftp       -wt 300 -ct 600",       0, $successOutput );
$t += checkCmd( "./check_ftp $host_nonresponsive -wt 0   -ct 0   -to 1", 2 );
$t += checkCmd( "./check_ftp $hostname_invalid   -wt 0   -ct 0",         2 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

