#! /usr/bin/perl -w -I ..
#
# DHCP Tests via check_dhcp
#

use strict;
use Test::More;
use NPTest;

my $allow_sudo = getTestParameter( "NP_ALLOW_SUDO",
                                   "If sudo is setup for this user to run any command as root ('yes' to allow)",
                                   "no" );

if ($allow_sudo eq "yes" or $> == 0) {
    plan tests => 8;
} else {
    plan skip_all => "Need sudo to test check_dhcp";
}
my $sudo = $> == 0 ? '' : 'sudo';

my $successOutput = '/Received \d+ DHCPOFFER(s)*, max lease time = \d+ seconds/';
my $failureOutput = '/(No DHCPOFFERs were received|Received \d+ DHCPOFFER\(s\), 0 of 1 requested servers responded, max lease time = \d+ sec\.)/';
my $invalidOutput = '/Invalid hostname/';

my $host_responsive    = getTestParameter( "NP_HOST_DHCP_RESPONSIVE",
                                           "The hostname of system responsive to dhcp requests",
                                           "localhost" );

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE",
                                           "The hostname of system not responsive to dhcp requests",
                                           "10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",
                                           "An invalid (not known to DNS) hostname",
                                           "nosuchhost" );

my $output_format = "--output-format mp-test-json";

# try to determince interface
my $interface = '';

# find interface used for default route
if (-x '/usr/sbin/ip' and `/usr/sbin/ip route get 1.1.1.1 2>/dev/null` =~ m/\sdev\s(\S+)/) {
    $interface = "-i $1";
}
elsif (`ifconfig -a 2>/dev/null` =~ m/^(e\w*\d+)/mx and $1 ne 'eth0') {
    $interface = ' -i '.$1;
}

my $res;
SKIP: {
    skip('need responsive test host', 2) unless $host_responsive;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $host_responsive $output_format"
    );
    is( $res->return_code, 0, "with JSON test format result should always be OK" );
    like( $res->{'mp_test_result'}->{'state'}, "/OK/", "Output OK" );
    like( $res->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $successOutput, "Output OK" );
};

SKIP: {
    skip('need nonresponsive test host', 2) unless $host_nonresponsive;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $host_nonresponsive $output_format"
    );
    is( $res->return_code, 0, "with JSON test format result should always be OK" );
    like( $res->{'mp_test_result'}->{'state'}, "/CRITICAL/", "Exit code - host nonresponsive" );
    like( $res->{'mp_test_result'}->{'checks'}->[0]->{'output'}, $failureOutput, "Output OK" );
};

SKIP: {
    skip('need invalid test host', 2) unless $hostname_invalid;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $hostname_invalid"
    );
    is( $res->return_code, 3, "invalid hostname/address should return UNKNOWN" );
    like( $res->output, $invalidOutput, "Output OK" );
};
