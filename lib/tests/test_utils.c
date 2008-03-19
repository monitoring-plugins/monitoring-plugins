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
* $Id$
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

	plan_tests(172);

	range = parse_range_string("6");
	ok( range != NULL, "'6' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = _parse_range_string_v2("6");
	ok( range == NULL, "Missing colon in range" );
	ok( utils_errno == NP_RANGE_MISSING_COLON, "Right error code" );
	
	range = _parse_range_string_v2("6:");
	ok( range != NULL, "'6:' is valid range");
	ok( range->start == 6, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end_infinity == TRUE, "Using infinity");
	free(range);

	range = _parse_range_string_v2("6:6");
	ok( range != NULL, "'6:6' is valid range");
	ok( range->start == 6, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = _parse_range_string_v2(":6");
	ok( range != NULL, "':6' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
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

	range = _parse_range_string_v2("1:12%%");
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

	range = _parse_range_string_v2("-7:23");
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

	range = _parse_range_string_v2(":5.75");
	ok( range != NULL, "':5.75' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == 5.75, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_range_string("~:-95.99");
	ok( range != NULL, "~:-95.99' is valid range");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == -95.99, "End correct (with rounding errors)");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = _parse_range_string_v2("~:-95.99");
	ok( range == NULL, "~:-95.99' is invalid range");
	ok( utils_errno == NP_RANGE_UNPARSEABLE, "Correct error code" );

	/*
	 * This is currently parseable. This is because ~ is interpreted as a 0
	 * and then 95.99 is the end, so we get 0:95.99. Should validate the characters before
	 * passing to strtod
	range = _parse_range_string_v2("~:95.99");
	ok( range == NULL, "~:95.99' is invalid range");
	ok( utils_errno == NP_RANGE_UNPARSEABLE, "Correct error code" );
	*/

	range = _parse_range_string_v2(":-95.99");
	ok( range != NULL, ":-95.99' is valid range");
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

	range = _parse_range_string_v2("12345678901234567890:");
	temp = atof("12345678901234567890");
	ok( range != NULL, "'12345678901234567890:' is valid range");
	ok( range->start == temp, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end_infinity == TRUE, "Using infinity");
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
	
	range = _parse_range_string_v2("-4.33:-4.33");
	ok( range != NULL, "'-4.33:-4.33' is valid range");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->start == -4.33, "Start right");
	ok( range->end == -4.33, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);
	
	range = parse_range_string("@0:657.8210567");
	ok( range != NULL, "@0:657.8210567' is a valid range");
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

	range = parse_range_string("^0:657.8210567");
	ok( range != NULL, "^0:657.8210567' is a valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 657.8210567, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
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

	range = _parse_range_string_v2("2:1");
	ok( range == NULL, "'2:1' rejected");
	ok( utils_errno == NP_RANGE_UNPARSEABLE, "Errno correct" );

	rc = _set_thresholds(&thresholds, NULL, NULL);
	ok( rc == 0, "Thresholds (NULL, NULL) set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical == NULL, "Critical not set");

	thresholds = _parse_thresholds_string(NULL);
	ok( thresholds != NULL, "Threshold still set, even though NULL");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical == NULL, "Critical set to NULL");

	thresholds = _parse_thresholds_string("");
	ok( thresholds != NULL, "Threshold still set, even though ''");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical == NULL, "Critical set to NULL");

	thresholds = _parse_thresholds_string("/");
	ok( thresholds != NULL, "Threshold still set, even though '/'");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical == NULL, "Critical set to NULL");

	rc = _set_thresholds(&thresholds, NULL, "80");
	ok( rc == 0, "Thresholds (NULL, '80') set");
	ok( thresholds->warning == NULL, "Warning not set");
	ok( thresholds->critical->end == 80, "Critical set correctly");

	thresholds = _parse_thresholds_string(":80/");
	ok( thresholds != NULL, "Threshold set for ':80/'");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical->start_infinity == TRUE, "Start is right" );
	ok( thresholds->critical->end == 80, "Critical set to 80");

	thresholds = _parse_thresholds_string(":80");
	ok( thresholds != NULL, "Threshold set for ':80'");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical->start_infinity == TRUE, "Start is right" );
	ok( thresholds->critical->end == 80, "Critical set to 80");

	thresholds = _parse_thresholds_string("80");
	ok( thresholds == NULL, "Threshold not set because of single value '80'");
	ok( utils_errno == NP_RANGE_MISSING_COLON, "Correct error message");

	rc = _set_thresholds(&thresholds, "5:33", NULL);
	ok( rc == 0, "Thresholds ('5:33', NULL) set");
	ok( thresholds->warning->start == 5, "Warning start set");
	ok( thresholds->warning->end == 33, "Warning end set");
	ok( thresholds->critical == NULL, "Critical not set");

	thresholds = _parse_thresholds_string("5:33");
	ok( thresholds != NULL, "Threshold set for '5:33'");
	ok( thresholds->warning == NULL, "Warning set to NULL");
	ok( thresholds->critical->start_infinity == FALSE, "Start is right" );
	ok( thresholds->critical->start == 5, "Critical set to 5");
	ok( thresholds->critical->end == 33, "Critical set to 33");

	thresholds = _parse_thresholds_string("/5:33");
	ok( thresholds != NULL, "Threshold set for '/5:33'");
	ok( thresholds->critical == NULL, "Critical set to NULL");
	ok( thresholds->warning->start_infinity == FALSE, "Start is right" );
	ok( thresholds->warning->start == 5, "Warning start set to 5");
	ok( thresholds->warning->end == 33, "Warning end set to 33");

	rc = _set_thresholds(&thresholds, "30", "60");
	ok( rc == 0, "Thresholds ('30', '60') set");
	ok( thresholds->warning->end == 30, "Warning set correctly");
	ok( thresholds->critical->end == 60, "Critical set correctly");
	ok( get_status(15.3, thresholds) == STATE_OK, "15.3 - ok");
	ok( get_status(30.0001, thresholds) == STATE_WARNING, "30.0001 - warning");
	ok( get_status(69, thresholds) == STATE_CRITICAL, "69 - critical");

	thresholds = _parse_thresholds_string("-6.7:29 / 235.4:3333.33");
	ok( thresholds != NULL, "Threshold set for '-6.7:29 / 235.4:3333.33'");
	ok( thresholds->critical->start_infinity == FALSE, "Critical not starting at infinity");
	ok( thresholds->critical->start == -6.7, "Critical start right" );
	ok( thresholds->critical->end_infinity == FALSE, "Critical not ending at infinity");
	ok( thresholds->critical->end == 29, "Critical end right" );
	ok( thresholds->warning->start_infinity == FALSE, "Start is right" );
	ok( thresholds->warning->start == 235.4, "Warning set to 5");
	ok( thresholds->warning->end_infinity == FALSE, "End is right" );
	ok( thresholds->warning->end == 3333.33, "Warning set to 33");

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
	free(test);

	test = basename("/here/is/a/path");
	ok( strcmp(test, "path") == 0, "basename okay");

	return exit_status();
}
