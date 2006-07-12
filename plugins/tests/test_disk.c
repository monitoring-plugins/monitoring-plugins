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

#include "common.h"
#include "utils_disk.h"
#include "tap.h"

int
main (int argc, char **argv)
{
	struct name_list *exclude_filesystem=NULL;
	struct name_list *exclude_fstype=NULL;

	plan_tests(8);

	ok( np_find_name(exclude_filesystem, "/var") == FALSE, "/var not in list");
	np_add_name(&exclude_filesystem, "/var");
	ok( np_find_name(exclude_filesystem, "/var") == TRUE, "is in list now");
	ok( np_find_name(exclude_filesystem, "/home") == FALSE, "/home not in list");
	np_add_name(&exclude_filesystem, "/home");
	ok( np_find_name(exclude_filesystem, "/home") == TRUE, "is in list now");
	ok( np_find_name(exclude_filesystem, "/var") == TRUE, "/var still in list");

	ok( np_find_name(exclude_fstype, "iso9660") == FALSE, "iso9660 not in list");
	np_add_name(&exclude_fstype, "iso9660");
	ok( np_find_name(exclude_fstype, "iso9660") == TRUE, "is in list now");

	ok( np_find_name(exclude_filesystem, "iso9660") == FALSE, "Make sure no clashing in variables");


	
	
	/*
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
	*/

	return exit_status();
}

