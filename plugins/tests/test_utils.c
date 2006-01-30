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
	threshold *range;
	double temp;

	plan_tests(40);

	range = parse_threshold("6");
	ok( range != NULL, "'6' is valid threshold");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 6, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_threshold("-7:23");
	ok( range != NULL, "'-7:23' is valid threshold");
	ok( range->start == -7, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 23, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_threshold(":5.75");
	ok( range != NULL, "':5.75' is valid threshold");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 5.75, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_threshold("~:-95.99");
	ok( range != NULL, "~:-95.99' is valid threshold");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == -95.99, "End correct (with rounding errors)");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_threshold("12345678901234567890:");
	temp = atof("12345678901234567890");		/* Can't just use this because number too large */
	ok( range != NULL, "'12345678901234567890:' is valid threshold");
	ok( range->start == temp, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end_infinity == TRUE, "Using infinity");
	free(range);

	range = parse_threshold("~:0");
	ok( range != NULL, "'~:0' is valid threshold");
	ok( range->start_infinity == TRUE, "Using negative infinity");
	ok( range->end == 0, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == OUTSIDE, "Will alert on outside of this range");
	free(range);
	
	range = parse_threshold("@0:657.8210567");
	ok( range != 0, "@0:657.8210567' is a valid threshold");
	ok( range->start == 0, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 657.8210567, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	ok( range->alert_on == INSIDE, "Will alert on inside of this range" );
	free(range);

	range = parse_threshold("1:1");
	ok( range != NULL, "'1:1' is a valid threshold");
	ok( range->start == 1, "Start correct");
	ok( range->start_infinity == FALSE, "Not using negative infinity");
	ok( range->end == 1, "End correct");
	ok( range->end_infinity == FALSE, "Not using infinity");
	free(range);

	range = parse_threshold("2:1");
	ok( range == NULL, "''2:1' rejected");

	return exit_status();
}

void print_usage() {
	printf("Dummy");
}
