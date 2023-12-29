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

my $host_tcp_ftp       = getTestParameter("NP_HOST_TCP_FTP", "A host providing the FTP Service (an FTP server)", "localhost");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my $successOutput = '/FTP OK -\s+[0-9]?\.?[0-9]+ second response time/';

my $t;

$t += checkCmd( "./check_ftp $host_tcp_ftp       -wt 300 -ct 600",       0, $successOutput );
$t += checkCmd( "./check_ftp $host_nonresponsive -wt 0   -ct 0   -to 1", 2 );
$t += checkCmd( "./check_ftp $hostname_invalid   -wt 0   -ct 0",         2 );

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);

