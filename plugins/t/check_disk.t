use strict;
use Test;
use vars qw($tests);

BEGIN {$tests = 6; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t;

$cmd = "./check_disk 100 100 /";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^(Disk ok - +[\.0-9]+|DISK OK - )/';

$cmd = "./check_disk -w 0 -c 0 /";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^(Disk ok - +[\.0-9]+|DISK OK - )/';

$cmd = "./check_disk 0 0 /";
$str = `$cmd`;
$t += ok $?>>8,2;
print "Test was: $cmd\n" unless ($?);
$t += ok $str, '/^(Only +[\.0-9]+|DISK CRITICAL - )/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
