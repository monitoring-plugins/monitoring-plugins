#!/usr/bin/perl
# cm@financial.com 07/2002
use strict;
use Net::SNMP;

if ($#ARGV ne 3) {
	print "Worng number of Arguments\n";
	exit 1;
}


my ($host, $device, $warnpercent, $errpercent) = @ARGV;
if ($warnpercent >= $errpercent) {
	print "Errorratio must be higher then Warnratio!\n";
	exit 1;
}

my ($session, $error) = Net::SNMP->session(
	-hostname	=>	$host,
	-nonblocking	=>	0x0,
	-username	=>	'XXXXX',
	-authpassword	=>	'XXXXXXXX',
	-authprotocol	=>	'md5',
	-version	=>	'3',
	);

if ($@) {
	print "SNMP-Error occured";
	exit 1;
}
my $result=undef;


my $deviceSize=".1.3.6.1.2.1.25.2.3.1.5.$device";
my $deviceUsed=".1.3.6.1.2.1.25.2.3.1.6.$device";
#my $deviceName=".1.3.6.1.2.1.25.3.7.1.2.1536.$device";
my $deviceName=".1.3.6.1.2.1.25.2.3.1.3.$device";
my @OID=($deviceSize, $deviceUsed, $deviceName);
$result = $session->get_request(
	-varbindlist => \@OID,
	);

if (!defined($result)) {
	printf("ERROR: %s.\n", $session->error);
	$session->close;
	exit 1;
}

my $ratio=$result->{$deviceUsed}*100/$result->{$deviceSize};

if ($ratio > $errpercent){
	printf("CRITICAL: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
	exit 2;
}
if ($ratio > $warnpercent){
	printf("WARNING: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
        exit 1;
}

printf("OK: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
exit 0;

