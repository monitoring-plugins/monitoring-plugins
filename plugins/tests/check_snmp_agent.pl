#! /usr/bin/perl -w -I ..
#
# Subagent for testing check_snmp
#

#use strict; # Doesn't work
use NetSNMP::OID qw(:all);
use NetSNMP::agent;
use NetSNMP::ASN qw(ASN_OCTET_STR ASN_COUNTER ASN_COUNTER64 ASN_INTEGER ASN_INTEGER64 ASN_UNSIGNED ASN_UNSIGNED64);
#use Math::Int64 qw(uint64); # Skip that module while we don't need it
sub uint64 { return $_ }

if (!$agent) {
	print "This program must run as an embedded NetSNMP agent\n";
	exit 1;
}

my $baseoid = '.1.3.6.1.4.1.8072.3.2.67';
my $multiline = 'Cisco Internetwork Operating System Software
IOS (tm) Catalyst 4000 "L3" Switch Software (cat4000-I9K91S-M), Version
12.2(20)EWA, RELEASE SOFTWARE (fc1)
Technical Support: http://www.cisco.com/techsupport
Copyright (c) 1986-2004 by cisco Systems, Inc.
';
my $multilin2 = "Kisco Outernetwork Oserating Gystem Totware
Copyleft (c) 2400-2689 by kisco Systrems, Inc.";
my $multilin3 = 'This should not confuse check_snmp "parser"
into thinking there is no 2nd line';
my $multilin4 = 'It\'s getting even harder if the line
ends with with this: C:\\';
my $multilin5 = 'And now have fun with with this: "C:\\"
because we\'re not done yet!';

# Next are arrays of indexes (Type, initial value and increments)
# 0..16 <---- please update comment when adding/removing fields
my @fields = (ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_UNSIGNED, ASN_UNSIGNED, ASN_COUNTER, ASN_COUNTER64, ASN_UNSIGNED, ASN_COUNTER, ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_OCTET_STR, ASN_INTEGER, ASN_OCTET_STR, ASN_OCTET_STR );
my @values = ($multiline, $multilin2, $multilin3, $multilin4, $multilin5, 4294965296, 1000, 4294965296, uint64("18446744073709351616"), int(rand(2**32)), 64000, "stringtests", "3.5", "87.4startswithnumberbutshouldbestring", '555"I said"', 'CUSTOM CHECK OK: foo is 12345', -2, '-4', '-6.6' );
# undef increments are randomized
my @incrts = (undef, undef, undef, undef, undef, 1000, -500, 1000, 100000, undef, 666, undef, undef, undef, undef, undef, -1, undef, undef );

# Number of elements in our OID
my $oidelts;
{
	my @oid = split(/\./, $baseoid);
	$oidelts = scalar(@oid);
}

my $regoid = new NetSNMP::OID($baseoid);
$agent->register('check_snmp_agent', $regoid, \&my_snmp_handler);

sub my_snmp_handler {
	my ($handler, $registration_info, $request_info, $requests) = @_;
	
	for (my $request = $requests; $request; $request = $request->next) {
		my $oid = $request->getOID();
		my $index;
		my $next_index;

		# Validate the OID
		my @numarray = $oid->to_array();
		if (@numarray != $oidelts) {
			if ($request_info->getMode() == MODE_GETNEXT && @numarray == ($oidelts - 1)) {
				# GETNEXT for the base oid; set it to the first index
				push(@numarray, 0);
				$next_index = 0;
			} else {
				# We got a request for the base OID or with extra elements
				$request->setError($request_info, SNMP_ERR_NOERROR);
				next;
			}
		}

		$index = pop(@numarray);
		if ($index >= scalar(@fields)) {
			# Index is out of bounds
			$request->setError($request_info, SNMP_ERR_NOERROR);
			next;
		}

		# Handle the request
		if ($request_info->getMode() == MODE_GETNEXT) {
			if (++$index >= scalar(@fields)) {
				# Index will grow out of bounds
				$request->setError($request_info, SNMP_ERR_NOERROR);
				next;
			}
			$index = (defined($next_index) ? $next_index : $index);
			$request->setOID("$baseoid.$index");
		} elsif ($request_info->getMode() != MODE_GET) {
			# Everything else is write-related modes
			$request->setError($request_info, SNMP_ERR_READONLY);
			next;
		}

		# Set the response... setValue is a bit touchy about the data type, but accepts plain strings.
		my $value = sprintf("%s", $values[$index]);
		$request->setValue($fields[$index], $value);

		# And update the value
		if (defined($incrts[$index])) {
			$values[$index] += $incrts[$index];
		} elsif ($fields[$index] != ASN_OCTET_STR) {
			my $minus = int(rand(2))*-1;
			$minus = 1 unless ($minus);
			my $exp = 32;
			$exp = 64 if ($fields[$index]  == ASN_COUNTER64 || $fields[$index] == ASN_INTEGER64 || $fields[$index] == ASN_UNSIGNED64);
			$values[$index] = int(rand(2**$exp));
		}
	}
}

