#!/usr/bin/perl
# cm@financial.com 07/2002
use strict;
use Net::SNMP;
use Getopt::Std;

my %opts =(
        u => 'nobody',          # snmp user
        l => 'authNoPriv',      # snmp security level   
        a => 'MD5',             # snmp authentication protocol
        A => 'nopass',          # authentication protocol pass phrase.
        x => 'DES',             # privacy protocol
        m => 'localhost',       # host 
        d => 1,         # devicenumber
        w => 70,                # warnratio
        c => 85,                # critical ratio
        h => 0,
        );

getopts('m:u:l:a:A:x:d:w:c:h',\%opts);

if ( $opts{'h'} ) {
	print "Usage: $0 [ -u <username> ] [ -l <snmp security level>] [ -a <snmp authentication protocol> ] [ -A <authentication protocol pass phrase> ] [ -x <snmp privacy protocol> ] [ -m <hostname>] [ -d <devicenumber> ] [ -w <warning ratio> ] [ -c <critical ratio ]\n";
	exit 1;
}

if ($opts{'w'} >= $opts{'c'}) {
	print "Errorratio must be higher then Warnratio!\n";
	exit 1;
}

my ($session, $error) = Net::SNMP->session(
	-hostname	=>	$opts{'m'},
	-nonblocking	=>	0x0,
	-username	=>	$opts{'u'},
	-authpassword	=>	$opts{'A'},
	-authprotocol	=>	$opts{'a'},
	-version	=>	'3',
	);

if ($@) {
	print "SNMP-Error occured";
	exit 1;
}
my $result=undef;


my $deviceSize=".1.3.6.1.2.1.25.2.3.1.5.$opts{'d'}";
my $deviceUsed=".1.3.6.1.2.1.25.2.3.1.6.$opts{'d'}";
my $deviceName=".1.3.6.1.2.1.25.2.3.1.3.$opts{'d'}";
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

if ($ratio > $opts{'c'}){
	printf("CRITICAL: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
	exit 2;
}
if ($ratio > $opts{'w'}){
	printf("WARNING: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
        exit 1;
}

printf("OK: %s usage %.2f%%\n", $result->{$deviceName}, $ratio);
exit 0;
