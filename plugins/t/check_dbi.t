#! /usr/bin/perl -w -I ..
#
# Database Server Tests via check_dbi
#
#
# Uses the 'sqlite3' DBD driver and command line utility.

use strict;
use Test::More;
use NPTest;

use File::Temp;

use vars qw($tests);

plan skip_all => "check_dbi not compiled" unless (-x "check_dbi");

$tests = 20;
plan tests => $tests;

my $missing_driver_output = "failed to open DBI driver 'sqlite3'";

my $bad_driver_output    = "/failed to open DBI driver 'nodriver'/";
my $conn_time_output     = "/OK - connection time: [0-9\.]+s \|/";
my $missing_query_output = "/Must specify a query to execute/";
my $no_rows_output       = "/WARNING - no rows returned/";
my $not_numeric_output   = "/CRITICAL - result value is not a numeric:/";
my $query_time_output    = "/OK - connection time: [0-9\.]+s, 'SELECT 1' returned 1.000000 in [0-9\.]+s \|/";
my $syntax_error_output  = "/CRITICAL - failed to execute query 'GET ALL FROM test': 1: near \"GET\": syntax error/";

my $result;

SKIP: {
	my $sqlite3 = qx(which sqlite3 2> /dev/null);
	chomp($sqlite3);

	skip "No Sqlite3 found", $tests unless $sqlite3;

	my $sqlite3_check = qx(./check_dbi -d sqlite3 -q '');
	if ($sqlite3_check =~ m/$missing_driver_output/) {
		skip "No 'sqlite3' DBD driver found", $tests;
	}

	my $fh = File::Temp->new(
		TEMPLATE => "/tmp/check_dbi_sqlite3.XXXXXXX",
		UNLINK   => 1,
	);
	my $filename = $fh->filename;
	$filename =~ s/^\/tmp\///;

	system("$sqlite3 /tmp/$filename 'CREATE TABLE test(a INT, b TEXT)'");
	system("$sqlite3 /tmp/$filename 'INSERT INTO test VALUES (1, \"text1\")'");
	system("$sqlite3 /tmp/$filename 'INSERT INTO test VALUES (2, \"text2\")'");

	my $check_cmd = "./check_dbi -d sqlite3 -o sqlite3_dbdir=/tmp -o dbname=$filename";

	$result = NPTest->testCmd("$check_cmd -q 'SELECT 1'");
	cmp_ok($result->return_code, '==', 0, "Sqlite3 login okay and can run query");

	$result = NPTest->testCmd("$check_cmd");
	cmp_ok($result->return_code, '==', 3, "Missing query parameter");
	like($result->output, $missing_query_output, "Missing query parameter error message");

	$result = NPTest->testCmd("$check_cmd -q 'GET ALL FROM test'");
	cmp_ok($result->return_code, '==', 2, "Invalid query");
	like($result->output, $syntax_error_output, "Syntax error message");

	$result = NPTest->testCmd("$check_cmd -q 'SELECT 2.71828' -w 2 -c 3");
	cmp_ok($result->return_code, '==', 1, "Got warning");

	$result = NPTest->testCmd("$check_cmd -q 'SELECT 3.1415' -w 2 -c 3");
	cmp_ok($result->return_code, '==', 2, "Got critical");

	$result = NPTest->testCmd("$check_cmd -q ''");
	cmp_ok($result->return_code, '==', 1, "No rows returned");
	like($result->output, $no_rows_output, "Now rows returned warning message");

	$result = NPTest->testCmd("$check_cmd -q 'SELECT b FROM test'");
	cmp_ok($result->return_code, '==', 2, "Value is not a numeric");
	like($result->output, $not_numeric_output, "Value is not a numeric error message");

	$result = NPTest->testCmd("$check_cmd -m QUERY_RESULT -q 'SELECT b FROM test' -e text1");
	cmp_ok($result->return_code, '==', 0, "Query result string comparison okay");

	$result = NPTest->testCmd("$check_cmd -q 'SELECT b FROM test' -r 'eXt[0-9]'");
	cmp_ok($result->return_code, '==', 2, "Query result case-insensitive regex failure");

	$result = NPTest->testCmd("$check_cmd -q 'SELECT b FROM test' -R 'eXt[0-9]'");
	cmp_ok($result->return_code, '==', 0, "Query result case-sensitive regex okay");

	$result = NPTest->testCmd("$check_cmd -m CONN_TIME -w 0.5 -c 0.7");
	cmp_ok($result->return_code, '==', 0, "CONN_TIME metric okay");
	like($result->output, $conn_time_output, "CONN_TIME metric output okay");

	$result = NPTest->testCmd("$check_cmd -m QUERY_TIME -q 'SELECT 1'");
	cmp_ok($result->return_code, '==', 0, "QUERY_TIME metric okay");
	like($result->output, $query_time_output, "QUERY_TIME metric output okay");

	$result = NPTest->testCmd("./check_dbi -d nodriver -q ''");
	cmp_ok($result->return_code, '==', 3, "Unknown DBI driver");
	like($result->output, $bad_driver_output, "Correct error message");
}

