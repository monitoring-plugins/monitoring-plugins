/*****************************************************************************
* 
* Library for check_tcp
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains utilities for check_tcp. These are tested by libtap
* 
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
#include "utils_tcp.h"

int
np_expect_match(char* status, char** server_expect, int expect_count, int flags)
{
	int match = 0;
	int i;
	for (i = 0; i < expect_count; i++) {
		if (flags & NP_MATCH_VERBOSE)
			printf ("looking for [%s] %s [%s]\n", server_expect[i],
					(flags & NP_MATCH_EXACT) ? "in beginning of" : "anywhere in",
					status);

		if ((flags & NP_MATCH_EXACT &&
			!strncmp(status, server_expect[i], strlen(server_expect[i]))) ||
			(!(flags & NP_MATCH_EXACT) && strstr(status, server_expect[i])))
		{
			if(flags & NP_MATCH_VERBOSE) puts("found it");
			match += 1;
		} else
			if(flags & NP_MATCH_VERBOSE) puts("couldn't find it");
	}
	if ((flags & NP_MATCH_ALL && match == expect_count) ||
		(!(flags & NP_MATCH_ALL) && match >= 1)) {
		return TRUE;
	} else
		return FALSE;
}
