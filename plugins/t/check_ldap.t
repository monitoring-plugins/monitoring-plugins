#!/usr/bin/env perl -I ..
#
# Lightweight Directory Access Protocol (LDAP) Test via check_ldap
#
#

use strict;
use warnings;
use Test::More;
use NPTest;

my $host_tcp_ldap      = getTestParameter("NP_HOST_TCP_LDAP", "A host providing the LDAP Service", "localhost");
my $ldap_base_dn       = getTestParameter("NP_LDAP_BASE_DN", "A base dn for the LDAP Service", "cn=admin");
my $host_nonresponsive = getTestParameter("NP_HOST_NONRESPONSIVE", "The hostname of system not responsive to network requests", "10.0.0.1");
my $hostname_invalid   = getTestParameter("NP_HOSTNAME_INVALID", "An invalid (not known to DNS) hostname", "nosuchhost");

my($result, $cmd);
my $command = './check_ldap';

plan tests => 16;

SKIP: {
    skip "NP_HOST_NONRESPONSIVE not set", 2 if ! $host_nonresponsive;

    $result = NPTest->testCmd("$command -H $host_nonresponsive -b ou=blah -t 2 -w 1 -c 1");
    is( $result->return_code, 2, "$command -H $host_nonresponsive -b ou=blah -t 5 -w 2 -c 3" );
    like($result->output, '/Socket timeout after \d+ seconds/', "output ok" );
};

SKIP: {
    skip "NP_HOSTNAME_INVALID not set", 2 if ! $hostname_invalid;

    $result = NPTest->testCmd("$command -H $hostname_invalid -b ou=blah -t 5");
    is( $result->return_code, 2, "$command -H $hostname_invalid -b ou=blah -t 5" );
    like( $result->output, '/could not bind to the LDAP server/', "output ok" );
};

SKIP: {
    skip "NP_HOST_TCP_LDAP not set", 12 if ! $host_tcp_ldap;
    skip "NP_LDAP_BASE_DN not set",  12 if ! $ldap_base_dn;

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 0, $cmd );
    like( $result->output, '/connection time \d+.\d+s/', "output ok" );

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3 -W 10000000 -C 10000001";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 0, $cmd );
    like( $result->output, '/found \d+ entries/', "output ok" );

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3 -W 10000000: -C 10000001:";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 2, $cmd );
    like( $result->output, '/found \d+ entries/', "output ok" );

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3 -W 0 -C 0";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 2, $cmd );
    like( $result->output, '/found \d+ entries/', "output ok" );

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3 -W 10000000: -C 10000001";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 1, $cmd );
    like( $result->output, '/found \d+ entries/', "output ok" );

    $cmd = "$command -H $host_tcp_ldap -b $ldap_base_dn -t 5 -w 2 -c 3 -3 -C 10000001";
    $result = NPTest->testCmd($cmd);
    is( $result->return_code, 0, $cmd );
    like( $result->output, '/found \d+ entries/', "output ok" );
};
