#! /usr/bin/perl -w -I ..
#
# MySQL Database Server Tests via check_mysql
#
#
#
# These are the database permissions required for this test:
#  GRANT SELECT ON $db.* TO $user@$host IDENTIFIED BY '$password';
#  GRANT SUPER, REPLICATION CLIENT ON *.* TO $user@$host;
# Check with:
#  mysql -u$user -p$password -h$host $db

use strict;
use Test::More;
use NPTest;

use vars qw($tests);

plan skip_all => "check_mysql not compiled" unless (-x "check_mysql");

plan tests => 15;

my $bad_login_output = '/Access denied for user /';
my $mysqlserver         = getTestParameter("NP_MYSQL_SERVER", "A MySQL Server hostname or IP with no replica setup");
my $mysqlsocket         = getTestParameter("NP_MYSQL_SOCKET", "Full path to a MySQL Server socket with no replica setup");
my $mysql_login_details = getTestParameter("NP_MYSQL_LOGIN_DETAILS", "Command line parameters to specify login access (requires REPLICATION CLIENT privileges)", "-u test -ptest");
my $with_replica          = getTestParameter("NP_MYSQL_WITH_REPLICA", "MySQL server with replica setup");
my $with_replica_login    = getTestParameter("NP_MYSQL_WITH_REPLICA_LOGIN", "Login details for server with replica (requires REPLICATION CLIENT privileges)", $mysql_login_details || "-u test -ptest");

my $result;

SKIP: {
	skip "No mysql server defined", 5 unless $mysqlserver;
	$result = NPTest->testCmd("./check_mysql -H $mysqlserver $mysql_login_details");
	cmp_ok( $result->return_code, '==', 0, "Login okay");

	$result = NPTest->testCmd("./check_mysql -H $mysqlserver -u dummy -pdummy");
	cmp_ok( $result->return_code, '==', 2, "Login failure");
	like( $result->output, $bad_login_output, "Expected login failure message");

	$result = NPTest->testCmd("./check_mysql -S -H $mysqlserver $mysql_login_details");
	cmp_ok( $result->return_code, "==", 1, "No replicas defined" );
	like( $result->output, "/No replicas defined/", "Correct error message");
}

SKIP: {
	skip "No mysql socket defined", 5 unless $mysqlsocket;
	$result = NPTest->testCmd("./check_mysql -s $mysqlsocket $mysql_login_details");
	cmp_ok( $result->return_code, '==', 0, "Login okay");

	$result = NPTest->testCmd("./check_mysql -s $mysqlsocket -u dummy -pdummy");
	cmp_ok( $result->return_code, '==', 2, "Login failure");
	like( $result->output, $bad_login_output, "Expected login failure message");

	$result = NPTest->testCmd("./check_mysql -S -s $mysqlsocket $mysql_login_details");
	cmp_ok( $result->return_code, "==", 1, "No replicas defined" );
	like( $result->output, "/No replicas defined/", "Correct error message");
}

SKIP: {
	skip "No mysql server with replicas defined", 5 unless $with_replica;
	$result = NPTest->testCmd("./check_mysql -H $with_replica $with_replica_login");
	cmp_ok( $result->return_code, '==', 0, "Login okay");

	$result = NPTest->testCmd("./check_mysql -S -H $with_replica $with_replica_login");
	cmp_ok( $result->return_code, "==", 0, "Replicas okay" );

	$result = NPTest->testCmd("./check_mysql -S -H $with_replica $with_replica_login -w 60");
	cmp_ok( $result->return_code, '==', 0, 'Replicas are not > 60 seconds behind');

	$result = NPTest->testCmd("./check_mysql -S -H $with_replica $with_replica_login -w 60:");
	cmp_ok( $result->return_code, '==', 1, 'Alert warning if < 60 seconds behind');
	like( $result->output, "/^SLOW_REPLICA WARNING:/", "Output okay");
}
