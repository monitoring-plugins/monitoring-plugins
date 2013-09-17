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
    plan tests => 6;
} else {
    plan skip_all => "Need sudo to test check_dhcp";
}
my $sudo = $> == 0 ? '' : 'sudo';

my $successOutput = '/OK: Received \d+ DHCPOFFER\(s\), \d+ of 1 requested servers responded, max lease time = \d+ sec\./';
my $failureOutput = '/CRITICAL: No DHCPOFFERs were received/';
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

# try to determince interface
my $interface = '';
if(`ifconfig -a 2>/dev/null` =~ m/^(e\w*\d+)/mx and $1 ne 'eth0') {
    $interface = ' -i '.$1;
}

my $res;
SKIP: {
    skip('need responsive test host', 2) unless $host_responsive;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $host_responsive"
    );
    is( $res->return_code, 0, "Syntax ok" );
    like( $res->output, $successOutput, "Output OK" );
};

SKIP: {
    skip('need nonresponsive test host', 2) unless $host_nonresponsive;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $host_nonresponsive"
    );
    is( $res->return_code, 2, "Exit code - host nonresponsive" );
    like( $res->output, $failureOutput, "Output OK" );
};

SKIP: {
    skip('need invalid test host', 2) unless $hostname_invalid;
    $res = NPTest->testCmd(
        "$sudo ./check_dhcp $interface -u -s $hostname_invalid"
    );
    is( $res->return_code, 3, "Exit code - host invalid" );
    like( $res->output, $invalidOutput, "Output OK" );
};
