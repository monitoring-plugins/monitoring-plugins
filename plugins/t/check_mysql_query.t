#! /usr/bin/perl -w -I ..
#
# MySQL Database Server Tests via check_mysql
#
#
#
# These are the database permissions required for this test:
#  GRANT SELECT ON $db.* TO $user@$host IDENTIFIED BY '$password';
# Check with:
#  mysql -u$user -p$password -h$host $db

use strict;
use Test::More;
use NPTest;

use vars qw($tests);

plan skip_all => "check_mysql_query not compiled" unless (-x "check_mysql_query");

my $mysqlserver = getTestParameter( 
		"NP_MYSQL_SERVER", 
		"A MySQL Server with no slaves setup"
		);
my $mysql_login_details = getTestParameter( 
		"MYSQL_LOGIN_DETAILS", 
		"Command line parameters to specify login access",
		"-u user -ppw -d db",
		);
my $result;

if (! $mysqlserver) {
	plan skip_all => "No mysql server defined";
} else {
	plan tests => 13;
}

$result = NPTest->testCmd("./check_mysql_query -q 'SELECT 1+1' -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 0, "Can run query");

$result = NPTest->testCmd("./check_mysql_query -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 3, "Missing query parmeter");
like( $result->output, "/Must specify a SQL query to run/", "Missing query error message");

$result = NPTest->testCmd("./check_mysql_query -q 'SELECT 1+1' -H $mysqlserver -u dummy -d mysql");
cmp_ok( $result->return_code, '==', 2, "Login failure");
like( $result->output, "/Access denied for user /", "Expected login failure message");

$result = NPTest->testCmd("./check_mysql_query -q 'SELECT PI()' -w 3 -c 4 -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 1, "Got warning");

$result = NPTest->testCmd("./check_mysql_query -q 'SELECT PI()*2' -w 3 -c 4 -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 2, "Got critical");

$result = NPTest->testCmd("./check_mysql_query -q 'SELECT * FROM adsf' -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 2, "Bad query");
like( $result->output, "/Error with query/", "Bad query error message");

$result = NPTest->testCmd("./check_mysql_query -q 'SHOW VARIABLES LIKE \"bob\"' -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 1, "No rows");
like( $result->output, "/No rows returned/", "No rows error message");

$result = NPTest->testCmd("./check_mysql_query -q 'SHOW VARIABLES' -H $mysqlserver $mysql_login_details");
cmp_ok( $result->return_code, '==', 2, "Data not numeric");
like( $result->output, "/Is not a numeric/", "Data not numeric error message");

