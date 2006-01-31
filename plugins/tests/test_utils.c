/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "utils";

#include "common.h"
#include "utils.h"
#include "popen.h"

#include "tap.h"

int
main (int argc, char **argv)
{
	range	*range;
	double	temp;
	thresholds *thresholds = NULL;
	int	rc;

	plan_tests(66);

	range = parse_range_string("6");
	ok( range != NULL, "'6' is valid range");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
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

	return exit_status();
}

void print_usage() {
	printf("Dummy");
}
