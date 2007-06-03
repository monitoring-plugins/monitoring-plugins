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
#include "utils_tcp.h"
#include "tap.h"

int
main (int argc, char **argv)
{
    int i;
	char** server_expect;
	char* status;
	bool verbose;
	bool exact_match;
	int server_expect_count = 3;
	plan_tests(8);

	server_expect = malloc(sizeof(char*) * server_expect_count);

	server_expect[0] = strdup("AA");
	server_expect[1] = strdup("bb");
	server_expect[2] = strdup("CC");
	
	ok(np_expect_match("AA bb CC XX", server_expect, server_expect_count, false, true, false) == true,
	   "Test matching any string at the beginning (first expect string)");
	ok(np_expect_match("bb AA CC XX", server_expect, server_expect_count, false, true, false) == true,
	   "Test matching any string at the beginning (second expect string)");
	ok(np_expect_match("XX bb AA CC XX", server_expect, server_expect_count, false, true, false) == false,
	   "Test with strings not matching at the beginning");
	ok(np_expect_match("XX CC XX", server_expect, server_expect_count, false, true, false) == false,
	   "Test matching any string");
	ok(np_expect_match("XX", server_expect, server_expect_count, false, false, false) == false,
	   "Test not matching any string");
	ok(np_expect_match("XX AA bb CC XX", server_expect, server_expect_count, true, false, false) == true,
	   "Test matching all strings");
	ok(np_expect_match("XX bb CC XX", server_expect, server_expect_count, true, false, false) == false,
	   "Test not matching all strings");
	ok(np_expect_match("XX XX", server_expect, server_expect_count, true, false, false) == false,
	   "Test not matching any string (testing all)");
	 

	return exit_status();
}

