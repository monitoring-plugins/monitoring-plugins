#! /usr/bin/perl -w -I ..
#
# check_ssh tests
#
#

use strict;
use Test::More;
use NPTest;

# Required parameters
my $ssh_host           = getTestParameter("NP_SSH_HOST",
                                          "A host providing SSH service",
                                          "localhost");

my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE",
                                          "The hostname of system not responsive to network requests",
                                          "10.0.0.1" );

my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID",
                                          "An invalid (not known to DNS) hostname",
                                          "nosuchhost" );


plan skip_all => "SSH_HOST must be defined" unless $ssh_host;
plan tests    => 6;


my $result = NPTest->testCmd(
    "./check_ssh -H $ssh_host"
    );
cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
like($result->output, '/^SSH OK - /', "Status text if command returned none (OK)");


$result = NPTest->testCmd(
    "./check_ssh -H $host_nonresponsive -t 2"
    );
cmp_ok($result->return_code, '==', 2, "Exit with return code 0 (OK)");
like($result->output, '/^CRITICAL - Socket timeout after 2 seconds/', "Status text if command returned none (OK)");



$result = NPTest->testCmd(
    "./check_ssh -H $hostname_invalid -t 2"
    );
cmp_ok($result->return_code, '==', 3, "Exit with return code 0 (OK)");
like($result->output, '/^check_ssh: Invalid hostname/', "Status text if command returned none (OK)");

