use strict;
use Test;
use vars qw($tests);

BEGIN {$tests = 2; plan tests => $tests}

my $null = '';
my $cmd;
my $str;
my $t=0;

$cmd = "./check_rpc -V";
$str = `$cmd`;
$t += ok $?>>8,0;
print "Test was: $cmd\n" if ($?);
$t += ok $str, '/^check_rpc/';

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
