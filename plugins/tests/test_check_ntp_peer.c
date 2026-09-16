#include "../../tap/tap.h"
#include "../check_ntp_peer.d/check_ntp_peer_helper.h"

#include "../../config.h"
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	plan_tests(23);
	/* check_ntp_peer_extract_ntpvar tests (23) */
	char *test;
	test = check_ntp_peer_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foo");
	ok(test && !strcmp(test, "bar"), "1st test as expected");
	free(test);

	test = check_ntp_peer_extract_ntpvar("foo=bar,bar=foo,foobar=barfoo\n", "bar");
	ok(test && !strcmp(test, "foo"), "2nd test as expected");
	free(test);

	test = check_ntp_peer_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foobar");
	ok(test && !strcmp(test, "barfoo"), "3rd test as expected");
	free(test);

	test = check_ntp_peer_extract_ntpvar("foo=bar\n", "foo");
	ok(test && !strcmp(test, "bar"), "Single test as expected");
	free(test);

	test = check_ntp_peer_extract_ntpvar("foo=bar, bar=foo, foobar=barfooi\n", "abcd");
	ok(!test, "Key not found 1");

	test = check_ntp_peer_extract_ntpvar("foo=bar\n", "abcd");
	ok(!test, "Key not found 2");

	test = check_ntp_peer_extract_ntpvar("foo=bar=foobar", "foo");
	ok(test && !strcmp(test, "bar=foobar"), "Strange string 1");
	free(test);

	test = check_ntp_peer_extract_ntpvar("foo", "foo");
	ok(!test, "Malformed string 1");

	test = check_ntp_peer_extract_ntpvar("foo,", "foo");
	ok(!test, "Malformed string 2");

	test = check_ntp_peer_extract_ntpvar("foo=", "foo");
	ok(!test, "Malformed string 3");

	test = check_ntp_peer_extract_ntpvar("foo=,bar=foo", "foo");
	ok(!test, "Malformed string 4");

	test = check_ntp_peer_extract_ntpvar(",foo", "foo");
	ok(!test, "Malformed string 5");

	test = check_ntp_peer_extract_ntpvar("=foo", "foo");
	ok(!test, "Malformed string 6");

	test = check_ntp_peer_extract_ntpvar("=foo,", "foo");
	ok(!test, "Malformed string 7");

	test = check_ntp_peer_extract_ntpvar(",,,", "foo");
	ok(!test, "Malformed string 8");

	test = check_ntp_peer_extract_ntpvar("===", "foo");
	ok(!test, "Malformed string 9");

	test = check_ntp_peer_extract_ntpvar(",=,=,", "foo");
	ok(!test, "Malformed string 10");

	test = check_ntp_peer_extract_ntpvar("=,=,=", "foo");
	ok(!test, "Malformed string 11");

	test = check_ntp_peer_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foo");
	ok(test && !strcmp(test, "bar"), "Random spaces and newlines 1");
	free(test);

	test = check_ntp_peer_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 2");
	free(test);

	test =
		check_ntp_peer_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foobar");
	ok(test && !strcmp(test, "barfoo"), "Random spaces and newlines 3");
	free(test);

	test = check_ntp_peer_extract_ntpvar("  foo=bar  ,\n bar\n \n= \n foo\n , foobar=barfoo  \n  ",
										 "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 4");
	free(test);

	test = check_ntp_peer_extract_ntpvar("", "foo");
	ok(!test, "Empty string return NULL");
}
