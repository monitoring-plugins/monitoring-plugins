#! /usr/bin/perl -w -I ..
#
# MySQL Database Server Tests via check_mysql
#
# $Id$
#

use strict;
use Test::More;
use NPTest;

use vars qw($tests);

plan skip_all => "check_mysql not compiled" unless (-x "check_mysql");

plan tests => 3;

my $failureOutput = '/Access denied for user /';
my $mysqlserver = getTestParameter( "mysql_server", "NP_MYSQL_SERVER", undef,
		"A MySQL Server");
my $mysql_login_details = getTestParameter( "mysql_login_details", "MYSQL_LOGIN_DETAILS", undef, 
		"Command line parameters to specify login access");

my $result;

$result = NPTest->testCmd("./check_mysql -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 0, "Login okay");

$result = NPTest->testCmd("./check_mysql -H $mysqlserver -u dummy");
cmp_ok( $result->return_code, '==', 2, "Login expected failure");
like( $result->output, $failureOutput, "Error string as expected");

