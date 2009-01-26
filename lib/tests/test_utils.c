/*****************************************************************************
* 
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
* 
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* 
* 
*****************************************************************************/

#include "common.h"
#include "utils_base.h"

#include "tap.h"

int
main (int argc, char **argv)
{
	range	*range;
	double	temp;
	thresholds *thresholds = NULL;
	int	rc;

	plan_tests(81+23);

	range = parse_range_string("6");
	ok( range != NULL, "'6' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("1:12%%");
	ok( range != NULL, "'1:12%%' is valid - percentages are ignored");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 12, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("-7:23");
	ok( range != NULL, "'-7:23' is valid range");
	ok( range->start == -7, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 23, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string(":5.75");
	ok( range != NULL, "':5.75' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 5.75, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("~:-95.99");
	ok( range != NULL, "~:-95.99' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == -95.99, "End correct (with rounding errors)");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("12345678901234567890:");
	temp = atof("12345678901234567890");		/* Can't just use this because number too large */
	ok( range != NULL, "'12345678901234567890:' is valid range");
	ok( range->start == temp, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end_infinity == TRUE, "Using infinity");
	/* Cannot do a "-1" on temp, as it appears to be same value */
	ok( check_range(temp/1.1, range) == TRUE, "12345678901234567890/1.1 - alert");
	ok( check_range(temp, range) == FALSE, "12345678901234567890 - no alert");
	ok( check_range(temp*2, range) == FALSE, "12345678901234567890*2 - no alert");
	free(range);

	range = parse_range_string("~:0");
	ok( range != NULL, "'~:0' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == 0, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == OUTSIDE, "Will alert on outside of this range");
	ok( check_range(0.5, range) == TRUE, "0.5 - alert");
	ok( check_range(-10, range) == FALSE, "-10 - no alert");
	ok( check_range(0, range)   == FALSE, "0 - no alert");
	free(range);
	
	range = parse_range_string("@0:657.8210567");
	ok( range != 0, "@0:657.8210567' is a valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 657.8210567, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == INSIDE, "Will alert on inside of this range" );
	ok( check_range(32.88, range) == TRUE, "32.88 - alert");
	ok( check_range(-2, range)    == FALSE, "-2 - no alert");
	ok( check_range(657.8210567, range) == TRUE, "657.8210567 - alert");
	ok( check_range(0, range)     == TRUE, "0 - alert");
	free(range);

	range = parse_range_string("1:1");
	ok( range != NULL, "'1:1' is a valid range");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 1, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( check_range(0.5, range) == TRUE, "0.5 - alert");
	ok( check_range(1, range) == FALSE, "1 - no alert");
	ok( check_range(5.2, range) == TRUE, "5.2 - alert");
	free(range);

	range = parse_range_string("2:1");
	ok( range == NULL, "'2:1' rejected");

	rc = _set_thresholds(&thresholds, NULL, NULL);
	ok( rc == 0, "Thresholds (NULL, NULL) set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical == NULL, "Critical not set");

	rc = _set_thresholds(&thresholds, NULL, "80");
	ok( rc == 0, "Thresholds (NULL, '80') set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical->end == 80, "Critical set correctly");

	rc = _set_thresholds(&thresholds, "5:33", NULL);
	ok( rc == 0, "Thresholds ('5:33', NULL) set");
	ok( thresholds->warning->start == 5, "Warning start set");
	ok( thresholds->warning->end == 33, "Warning end set");
	ok( thresholds->critical == NULL, "Critical not set");

	rc = _set_thresholds(&thresholds, "30", "60");
	ok( rc == 0, "Thresholds ('30', '60') set");
	ok( thresholds->warning->end == 30, "Warning set correctly");
	ok( thresholds->critical->end == 60, "Critical set correctly");
	ok( get_status(15.3, thresholds) == STATE_OK, "15.3 - ok");
	ok( get_status(30.0001, thresholds) == STATE_WARNING, "30.0001 - warning");
	ok( get_status(69, thresholds) == STATE_CRITICAL, "69 - critical");

	char *test;
	test = np_escaped_string("bob\\n");
	ok( strcmp(test, "bob\n") == 0, "bob\\n ok");
	free(test);

	test = np_escaped_string("rhuba\\rb");
	ok( strcmp(test, "rhuba\rb") == 0, "rhuba\\rb okay");
	free(test);

	test = np_escaped_string("ba\\nge\\r");
	ok( strcmp(test, "ba\nge\r") == 0, "ba\\nge\\r okay");
	free(test);

	test = np_escaped_string("\\rabbi\\t");
	ok( strcmp(test, "\rabbi\t") == 0, "\\rabbi\\t okay");
	free(test);

	test = np_escaped_string("and\\\\or");
	ok( strcmp(test, "and\\or") == 0, "and\\\\or okay");
	free(test);

	test = np_escaped_string("bo\\gus");
	ok( strcmp(test, "bogus") == 0, "bo\\gus okay");
	free(test);

	test = np_escaped_string("everything");
	ok( strcmp(test, "everything") == 0, "everything okay");

	/* np_extract_ntpvar tests (23) */
	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foo");
	ok(test && !strcmp(test, "bar"), "1st test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar,bar=foo,foobar=barfoo\n", "bar");
	ok(test && !strcmp(test, "foo"), "2nd test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foobar");
	ok(test && !strcmp(test, "barfoo"), "3rd test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar\n", "foo");
	ok(test && !strcmp(test, "bar"), "Single test as expected");
	free(test);

	test=np_extract_ntpvar("foo=bar, bar=foo, foobar=barfooi\n", "abcd");
	ok(!test, "Key not found 1");

	test=np_extract_ntpvar("foo=bar\n", "abcd");
	ok(!test, "Key not found 2");

	test=np_extract_ntpvar("foo=bar=foobar", "foo");
	ok(test && !strcmp(test, "bar=foobar"), "Strange string 1");
	free(test);

	test=np_extract_ntpvar("foo", "foo");
	ok(!test, "Malformed string 1");

	test=np_extract_ntpvar("foo,", "foo");
	ok(!test, "Malformed string 2");

	test=np_extract_ntpvar("foo=", "foo");
	ok(!test, "Malformed string 3");

	test=np_extract_ntpvar("foo=,bar=foo", "foo");
	ok(!test, "Malformed string 4");

	test=np_extract_ntpvar(",foo", "foo");
	ok(!test, "Malformed string 5");

	test=np_extract_ntpvar("=foo", "foo");
	ok(!test, "Malformed string 6");

	test=np_extract_ntpvar("=foo,", "foo");
	ok(!test, "Malformed string 7");

	test=np_extract_ntpvar(",,,", "foo");
	ok(!test, "Malformed string 8");

	test=np_extract_ntpvar("===", "foo");
	ok(!test, "Malformed string 9");

	test=np_extract_ntpvar(",=,=,", "foo");
	ok(!test, "Malformed string 10");

	test=np_extract_ntpvar("=,=,=", "foo");
	ok(!test, "Malformed string 11");

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foo");
	ok(test && !strcmp(test, "bar"), "Random spaces and newlines 1");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 2");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foobar");
	ok(test && !strcmp(test, "barfoo"), "Random spaces and newlines 3");
	free(test);

	test=np_extract_ntpvar("  foo=bar  ,\n bar\n \n= \n foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 4");
	free(test);

	test=np_extract_ntpvar("", "foo");
	ok(!test, "Empty string return NULL");

	return exit_status();
}
