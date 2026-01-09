#! /usr/bin/perl -w -I ..
#
# Subagent for testing check_snmp
#

#use strict; # Doesn't work
use warnings;
use NetSNMP::OID qw(:all);
use NetSNMP::agent;
use NetSNMP::ASN qw(ASN_OCTET_STR ASN_COUNTER ASN_COUNTER64 ASN_INTEGER ASN_INTEGER64 ASN_UNSIGNED ASN_UNSIGNED64 ASN_FLOAT);
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
my $multiline2 = "Kisco Outernetwork Oserating Gystem Totware
Copyleft (c) 2400-2689 by kisco Systrems, Inc.";
my $multiline3 = 'This should not confuse check_snmp "parser"
into thinking there is no 2nd line';
my $multiline4 = 'It\'s getting even harder if the line
ends with with this: C:\\';
my $multiline5 = 'And now have fun with with this: "C:\\"
because we\'re not done yet!';

# Next are arrays of indexes (Type, initial value and increments)
# 0..19 <---- please update comment when adding/removing fields
my @fields = (ASN_OCTET_STR, # 0
							ASN_OCTET_STR, # 1
 							ASN_OCTET_STR, # 2
							ASN_OCTET_STR, # 3
							ASN_OCTET_STR, # 4
							ASN_UNSIGNED, #  5
							ASN_UNSIGNED, #  6
							ASN_COUNTER, #   7
							ASN_COUNTER64, # 8
							ASN_UNSIGNED, #  9
							ASN_COUNTER, #  10
							ASN_OCTET_STR, # 11
							ASN_OCTET_STR, # 12
							ASN_OCTET_STR, # 13
							ASN_OCTET_STR, # 14
							ASN_OCTET_STR, # 15
							ASN_INTEGER, #   16
							ASN_INTEGER, # 17
							ASN_FLOAT, # 18
							ASN_INTEGER #    19
							);
my @values = ($multiline,  # 0
							$multiline2,  # 1
							$multiline3,  # 2
							$multiline4,  # 3
							$multiline5,  # 4
							4294965296,  # 5
							1000,  #       6
							4294965296,  # 7
							uint64("18446744073709351616"),  # 8
							int(rand(2**32)),  # 9
							64000,  # 10
							"stringtests",  # 11
							"3.5",  # 12
							"87.4startswithnumberbutshouldbestring",  # 13
							'555"I said"',  # 14
							'CUSTOM CHECK OK: foo is 12345',  # 15
							'-2',  # 16
							'-4',  # 17
							'-6.6',  # 18
							42  # 19
						);
# undef increments are randomized
my @incrts = (
							undef, # 0
							undef, # 1
							undef, # 2
							undef, # 3
							undef, # 4
							1000, # 5
							-500, # 6
							1000, # 7
							100000, # 8
							undef, # 9
							666, # 10
							undef, # 11
							undef, # 12
							undef, # 13
							undef, # 14
							undef, # 15
							-1, #    16
							0, # 17
							undef, # 18
							0 #      19
					 );

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

