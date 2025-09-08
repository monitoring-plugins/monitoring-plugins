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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "utils_base.c"

int main(int argc, char **argv) {
	plan_tests(155);

	ok(this_monitoring_plugin == NULL, "monitoring_plugin not initialised");

	np_init("check_test", argc, argv);

	ok(this_monitoring_plugin != NULL, "monitoring_plugin now initialised");
	ok(!strcmp(this_monitoring_plugin->plugin_name, "check_test"), "plugin name initialised");

	ok(this_monitoring_plugin->argc == argc, "Argc set");
	ok(this_monitoring_plugin->argv == argv, "Argv set");

	np_set_args(0, 0);

	ok(this_monitoring_plugin->argc == 0, "argc changed");
	ok(this_monitoring_plugin->argv == 0, "argv changed");

	np_set_args(argc, argv);

	range *range = parse_range_string("6");
	ok(range != NULL, "'6' is valid range");
	ok(range->start == 0, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 6, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	free(range);

	range = parse_range_string("1:12%%");
	ok(range != NULL, "'1:12%%' is valid - percentages are ignored");
	ok(range->start == 1, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 12, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	free(range);

	range = parse_range_string("-7:23");
	ok(range != NULL, "'-7:23' is valid range");
	ok(range->start == -7, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 23, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	free(range);

	range = parse_range_string(":5.75");
	ok(range != NULL, "':5.75' is valid range");
	ok(range->start == 0, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 5.75, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	free(range);

	range = parse_range_string("~:-95.99");
	ok(range != NULL, "~:-95.99' is valid range");
	ok(range->start_infinity == true, "Using negative infinity");
	ok(range->end == -95.99, "End correct (with rounding errors)");
	ok(range->end_infinity == false, "Not using infinity");
	free(range);

	range = parse_range_string("12345678901234567890:");
	double temp = atof("12345678901234567890"); /* Can't just use this because number too large */
	ok(range != NULL, "'12345678901234567890:' is valid range");
	ok(range->start == temp, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end_infinity == true, "Using infinity");
	/* Cannot do a "-1" on temp, as it appears to be same value */
	ok(check_range(temp / 1.1, range) == true, "12345678901234567890/1.1 - alert");
	ok(check_range(temp, range) == false, "12345678901234567890 - no alert");
	ok(check_range(temp * 2, range) == false, "12345678901234567890*2 - no alert");
	free(range);

	range = parse_range_string("~:0");
	ok(range != NULL, "'~:0' is valid range");
	ok(range->start_infinity == true, "Using negative infinity");
	ok(range->end == 0, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	ok(range->alert_on == OUTSIDE, "Will alert on outside of this range");
	ok(check_range(0.5, range) == true, "0.5 - alert");
	ok(check_range(-10, range) == false, "-10 - no alert");
	ok(check_range(0, range) == false, "0 - no alert");
	free(range);

	range = parse_range_string("@0:657.8210567");
	ok(range != 0, "@0:657.8210567' is a valid range");
	ok(range->start == 0, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 657.8210567, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	ok(range->alert_on == INSIDE, "Will alert on inside of this range");
	ok(check_range(32.88, range) == true, "32.88 - alert");
	ok(check_range(-2, range) == false, "-2 - no alert");
	ok(check_range(657.8210567, range) == true, "657.8210567 - alert");
	ok(check_range(0, range) == true, "0 - alert");
	free(range);

	range = parse_range_string("@1:1");
	ok(range != NULL, "'@1:1' is a valid range");
	ok(range->start == 1, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 1, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	ok(range->alert_on == INSIDE, "Will alert on inside of this range");
	ok(check_range(0.5, range) == false, "0.5 - no alert");
	ok(check_range(1, range) == true, "1 - alert");
	ok(check_range(5.2, range) == false, "5.2 - no alert");
	free(range);

	range = parse_range_string("1:1");
	ok(range != NULL, "'1:1' is a valid range");
	ok(range->start == 1, "Start correct");
	ok(range->start_infinity == false, "Not using negative infinity");
	ok(range->end == 1, "End correct");
	ok(range->end_infinity == false, "Not using infinity");
	ok(check_range(0.5, range) == true, "0.5 - alert");
	ok(check_range(1, range) == false, "1 - no alert");
	ok(check_range(5.2, range) == true, "5.2 - alert");
	free(range);

	range = parse_range_string("2:1");
	ok(range == NULL, "'2:1' rejected");

	thresholds *thresholds = NULL;
	int returnCode;
	returnCode = _set_thresholds(&thresholds, NULL, NULL);
	ok(returnCode == 0, "Thresholds (NULL, NULL) set");
	ok(thresholds->warning == NULL, "Warning not set");
	ok(thresholds->critical == NULL, "Critical not set");

	returnCode = _set_thresholds(&thresholds, NULL, "80");
	ok(returnCode == 0, "Thresholds (NULL, '80') set");
	ok(thresholds->warning == NULL, "Warning not set");
	ok(thresholds->critical->end == 80, "Critical set correctly");

	returnCode = _set_thresholds(&thresholds, "5:33", NULL);
	ok(returnCode == 0, "Thresholds ('5:33', NULL) set");
	ok(thresholds->warning->start == 5, "Warning start set");
	ok(thresholds->warning->end == 33, "Warning end set");
	ok(thresholds->critical == NULL, "Critical not set");

	returnCode = _set_thresholds(&thresholds, "30", "60");
	ok(returnCode == 0, "Thresholds ('30', '60') set");
	ok(thresholds->warning->end == 30, "Warning set correctly");
	ok(thresholds->critical->end == 60, "Critical set correctly");
	ok(get_status(15.3, thresholds) == STATE_OK, "15.3 - ok");
	ok(get_status(30.0001, thresholds) == STATE_WARNING, "30.0001 - warning");
	ok(get_status(69, thresholds) == STATE_CRITICAL, "69 - critical");

	returnCode = _set_thresholds(&thresholds, "-10:-2", "-30:20");
	ok(returnCode == 0, "Thresholds ('-30:20', '-10:-2') set");
	ok(thresholds->warning->start == -10, "Warning start set correctly");
	ok(thresholds->warning->end == -2, "Warning end set correctly");
	ok(thresholds->critical->start == -30, "Critical start set correctly");
	ok(thresholds->critical->end == 20, "Critical end set correctly");
	ok(get_status(-31, thresholds) == STATE_CRITICAL, "-31 - critical");
	ok(get_status(-29, thresholds) == STATE_WARNING, "-29 - warning");
	ok(get_status(-11, thresholds) == STATE_WARNING, "-11 - warning");
	ok(get_status(-10, thresholds) == STATE_OK, "-10 - ok");
	ok(get_status(-2, thresholds) == STATE_OK, "-2 - ok");
	ok(get_status(-1, thresholds) == STATE_WARNING, "-1 - warning");
	ok(get_status(19, thresholds) == STATE_WARNING, "19 - warning");
	ok(get_status(21, thresholds) == STATE_CRITICAL, "21 - critical");

	char *test;
	test = np_escaped_string("bob\\n");
	ok(strcmp(test, "bob\n") == 0, "bob\\n ok");
	free(test);

	test = np_escaped_string("rhuba\\rb");
	ok(strcmp(test, "rhuba\rb") == 0, "rhuba\\rb okay");
	free(test);

	test = np_escaped_string("ba\\nge\\r");
	ok(strcmp(test, "ba\nge\r") == 0, "ba\\nge\\r okay");
	free(test);

	test = np_escaped_string("\\rabbi\\t");
	ok(strcmp(test, "\rabbi\t") == 0, "\\rabbi\\t okay");
	free(test);

	test = np_escaped_string("and\\\\or");
	ok(strcmp(test, "and\\or") == 0, "and\\\\or okay");
	free(test);

	test = np_escaped_string("bo\\gus");
	ok(strcmp(test, "bogus") == 0, "bo\\gus okay");
	free(test);

	test = np_escaped_string("everything");
	ok(strcmp(test, "everything") == 0, "everything okay");

	/* np_extract_ntpvar tests (23) */
	test = np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foo");
	ok(test && !strcmp(test, "bar"), "1st test as expected");
	free(test);

	test = np_extract_ntpvar("foo=bar,bar=foo,foobar=barfoo\n", "bar");
	ok(test && !strcmp(test, "foo"), "2nd test as expected");
	free(test);

	test = np_extract_ntpvar("foo=bar, bar=foo, foobar=barfoo\n", "foobar");
	ok(test && !strcmp(test, "barfoo"), "3rd test as expected");
	free(test);

	test = np_extract_ntpvar("foo=bar\n", "foo");
	ok(test && !strcmp(test, "bar"), "Single test as expected");
	free(test);

	test = np_extract_ntpvar("foo=bar, bar=foo, foobar=barfooi\n", "abcd");
	ok(!test, "Key not found 1");

	test = np_extract_ntpvar("foo=bar\n", "abcd");
	ok(!test, "Key not found 2");

	test = np_extract_ntpvar("foo=bar=foobar", "foo");
	ok(test && !strcmp(test, "bar=foobar"), "Strange string 1");
	free(test);

	test = np_extract_ntpvar("foo", "foo");
	ok(!test, "Malformed string 1");

	test = np_extract_ntpvar("foo,", "foo");
	ok(!test, "Malformed string 2");

	test = np_extract_ntpvar("foo=", "foo");
	ok(!test, "Malformed string 3");

	test = np_extract_ntpvar("foo=,bar=foo", "foo");
	ok(!test, "Malformed string 4");

	test = np_extract_ntpvar(",foo", "foo");
	ok(!test, "Malformed string 5");

	test = np_extract_ntpvar("=foo", "foo");
	ok(!test, "Malformed string 6");

	test = np_extract_ntpvar("=foo,", "foo");
	ok(!test, "Malformed string 7");

	test = np_extract_ntpvar(",,,", "foo");
	ok(!test, "Malformed string 8");

	test = np_extract_ntpvar("===", "foo");
	ok(!test, "Malformed string 9");

	test = np_extract_ntpvar(",=,=,", "foo");
	ok(!test, "Malformed string 10");

	test = np_extract_ntpvar("=,=,=", "foo");
	ok(!test, "Malformed string 11");

	test = np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foo");
	ok(test && !strcmp(test, "bar"), "Random spaces and newlines 1");
	free(test);

	test = np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 2");
	free(test);

	test = np_extract_ntpvar("  foo=bar  ,\n bar=foo\n , foobar=barfoo  \n  ", "foobar");
	ok(test && !strcmp(test, "barfoo"), "Random spaces and newlines 3");
	free(test);

	test = np_extract_ntpvar("  foo=bar  ,\n bar\n \n= \n foo\n , foobar=barfoo  \n  ", "bar");
	ok(test && !strcmp(test, "foo"), "Random spaces and newlines 4");
	free(test);

	test = np_extract_ntpvar("", "foo");
	ok(!test, "Empty string return NULL");

	ok(mp_suid() == false, "Test aren't suid");

	/* base states with random case */
	char *states[] = {"Ok", "wArnINg", "cRiTIcaL", "UnKNoWN", NULL};

	for (int i = 0; states[i] != NULL; i++) {
		/* out of the random case states, create the lower and upper versions + numeric string one
		 */
		char *statelower = strdup(states[i]);
		char *stateupper = strdup(states[i]);
		char statenum[2];
		for (char *temp_ptr = statelower; *temp_ptr; temp_ptr++) {
			*temp_ptr = (char)tolower(*temp_ptr);
		}
		for (char *temp_ptr = stateupper; *temp_ptr; temp_ptr++) {
			*temp_ptr = (char)toupper(*temp_ptr);
		}
		snprintf(statenum, 2, "%i", i);

		/* Base test names, we'll append the state string */
		char testname[64] = "Translate state string: ";
		size_t tlen = strlen(testname);

		strcpy(testname + tlen, states[i]);
		ok(i == mp_translate_state(states[i]), testname);

		strcpy(testname + tlen, statelower);
		ok(i == mp_translate_state(statelower), testname);

		strcpy(testname + tlen, stateupper);
		ok(i == mp_translate_state(stateupper), testname);

		strcpy(testname + tlen, statenum);
		ok(i == mp_translate_state(statenum), testname);
	}
	ok(ERROR == mp_translate_state("warningfewgw"), "Translate state string with garbage");
	ok(ERROR == mp_translate_state("00"), "Translate state string: bad numeric string 1");
	ok(ERROR == mp_translate_state("01"), "Translate state string: bad numeric string 2");
	ok(ERROR == mp_translate_state("10"), "Translate state string: bad numeric string 3");
	ok(ERROR == mp_translate_state(""), "Translate state string: empty string");

	return exit_status();
}
