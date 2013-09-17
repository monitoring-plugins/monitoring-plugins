#! /usr/bin/perl -w -I ..
#
# check_by_ssh tests
#
#

use strict;
use Test::More;
use NPTest;

# Required parameters
my $ssh_service = getTestParameter( "NP_SSH_HOST",
    "A host providing SSH service",
    "localhost");

my $ssh_key = getTestParameter( "NP_SSH_IDENTITY",
    "A key allowing access to NP_SSH_HOST",
    "~/.ssh/id_dsa");

my $ssh_conf = getTestParameter( "NP_SSH_CONFIGFILE",
    "A config file with ssh settings",
    "~/.ssh/config");


plan skip_all => "SSH_HOST and SSH_IDENTITY must be defined" unless ($ssh_service && $ssh_key);

plan tests => 42;

# Some random check strings/response
my @responce = ('OK: Everything is fine',
                'WARNING: Hey, pick me, pick me',
                'CRITICAL: Shit happens',
                'UNKNOWN: What can I do for ya',
                'WOOPS: What did I smoke',
);
my @responce_re;
my @check;
for (@responce) {
	push(@check, "echo $_");
	my $re_str = $_;
	$re_str =~ s{(.)} { "\Q$1" }ge;
	push(@responce_re, $re_str);
}

my $result;

# expand paths
$ssh_key  = glob($ssh_key)  if $ssh_key;
$ssh_conf = glob($ssh_conf) if $ssh_conf;

## Single active checks

for (my $i=0; $i<4; $i++) {
	$result = NPTest->testCmd(
		"./check_by_ssh -i $ssh_key -H $ssh_service -C '$check[$i]; exit $i'"
		);
	cmp_ok($result->return_code, '==', $i, "Exit with return code $i");
	is($result->output, $responce[$i], "Status text is correct for check $i");
}

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C 'exit 0'"
	);
cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
is($result->output, 'OK - check_by_ssh: Remote command \'exit 0\' returned status 0', "Status text if command returned none (OK)");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C 'exit 1'"
	);
cmp_ok($result->return_code, '==', 1, "Exit with return code 1 (WARNING)");
is($result->output, 'WARNING - check_by_ssh: Remote command \'exit 1\' returned status 1', "Status text if command returned none (WARNING)");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C 'exit 2'"
	);
cmp_ok($result->return_code, '==', 2, "Exit with return code 2 (CRITICAL)");
is($result->output, 'CRITICAL - check_by_ssh: Remote command \'exit 2\' returned status 2', "Status text if command returned none (CRITICAL)");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C 'exit 3'"
	);
cmp_ok($result->return_code, '==', 3, "Exit with return code 3 (UNKNOWN)");
is($result->output, 'UNKNOWN - check_by_ssh: Remote command \'exit 3\' returned status 3', "Status text if command returned none (UNKNOWN)");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C 'exit 7'"
	);
cmp_ok($result->return_code, '==', 7, "Exit with return code 7 (out of bounds)");
is($result->output, 'UNKNOWN - check_by_ssh: Remote command \'exit 7\' returned status 7', "Status text if command returned none (out of bounds)");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C '$check[4]; exit 8'"
	);
cmp_ok($result->return_code, '==', 8, "Exit with return code 8 (out of bounds)");
is($result->output, $responce[4], "Return proper status text even with unknown status codes");

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -F $ssh_conf -C 'exit 0'"
	);
cmp_ok($result->return_code, '==', 0, "Exit with return code 0 (OK)");
is($result->output, 'OK - check_by_ssh: Remote command \'exit 0\' returned status 0', "Status text if command returned none (OK)");

# Multiple active checks
$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -C '$check[1]; sh -c exit\\ 1'  -C '$check[0]; sh -c exit\\ 0' -C '$check[3]; sh -c exit\\ 3' -C '$check[2]; sh -c exit\\ 2'"
	);
cmp_ok($result->return_code, '==', 0, "Multiple checks always return OK");
my @lines = split(/\n/, $result->output);
cmp_ok(scalar(@lines), '==', 8, "Correct number of output lines for multiple checks");
my %linemap = (
               '0' => '1',
               '2' => '0',
               '4' => '3',
               '6' => '2',
);
foreach my $line (0, 2, 4, 6) {
	my $code = $linemap{$line};
	my $statline = $line+1;
	is($lines[$line], "$responce[$code]", "multiple checks status text is correct for line $line");
	is($lines[$statline], "STATUS CODE: $code", "multiple check status code is correct for line $line");
}

# Passive checks
unlink("/tmp/check_by_ssh.$$");
$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -n flint -s serv -C '$check[2]; sh -c exit\\ 2' -O /tmp/check_by_ssh.$$"
	);
cmp_ok($result->return_code, '==', 0, "Exit always ok on passive checks");
open(PASV, "/tmp/check_by_ssh.$$") or die("Unable to open '/tmp/check_by_ssh.$$': $!");
my @pasv = <PASV>;
close(PASV) or die("Unable to close '/tmp/check_by_ssh.$$': $!");
cmp_ok(scalar(@pasv), '==', 1, 'One passive result for one check performed');
for (0) {
	if ($pasv[$_]) {
		like($pasv[$_], '/^\[\d+\] PROCESS_SERVICE_CHECK_RESULT;flint;serv;2;' . $responce_re[2] . '$/', 'proper result for passive check');
	} else {
		fail('proper result for passive check');
	}
}
unlink("/tmp/check_by_ssh.$$") or die("Unable to unlink '/tmp/check_by_ssh.$$': $!");
undef @pasv;

$result = NPTest->testCmd(
	"./check_by_ssh -i $ssh_key -H $ssh_service -n flint -s c0:c1:c2:c3:c4 -C '$check[0];sh -c exit\\ 0' -C '$check[1];sh -c exit\\ 1' -C '$check[2];sh -c exit\\ 2' -C '$check[3];sh -c exit\\ 3' -C '$check[4];sh -c exit\\ 9' -O /tmp/check_by_ssh.$$"
	);
cmp_ok($result->return_code, '==', 0, "Exit always ok on passive checks");
open(PASV, "/tmp/check_by_ssh.$$") or die("Unable to open '/tmp/check_by_ssh.$$': $!");
@pasv = <PASV>;
close(PASV) or die("Unable to close '/tmp/check_by_ssh.$$': $!");
cmp_ok(scalar(@pasv), '==', 5, 'Five passive result for five checks performed');
for (0, 1, 2, 3, 4) {
	if ($pasv[$_]) {
		my $ret = $_;
		$ret = 9 if ($_ == 4);
		like($pasv[$_], '/^\[\d+\] PROCESS_SERVICE_CHECK_RESULT;flint;c' . $_ . ';' . $ret . ';' . $responce_re[$_] . '$/', "proper result for passive check $_");
	} else {
		fail("proper result for passive check $_");
	}
}
unlink("/tmp/check_by_ssh.$$") or die("Unable to unlink '/tmp/check_by_ssh.$$': $!");

