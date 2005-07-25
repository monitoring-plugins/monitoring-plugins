#! /usr/bin/perl -w -I ..
#
# MySQL Database Server Tests via check_mysql
#
# $Id$
#

use strict;
use Test;
use NPTest;

use vars qw($tests);

BEGIN {$tests = 2; plan tests => $tests}

my $t;

my $failureOutput = '/Access denied for user: /';

if ( -x "./check_mysql" )
{
  my $mysqlserver = getTestParameter( "mysql_server", "NP_MYSQL_SERVER", undef,
				      "A MySQL Server");

  $t += checkCmd( "./check_mysql -H $mysqlserver -P 3306", 2, $failureOutput );
}
else
{
  $t += skipMissingCmd( "./check_mysql", $tests );
}

exit(0) if defined($Test::Harness::VERSION);
exit($tests - $t);
