/*****************************************************************************
 *
 * License: GPL
 * Copyright (c) 2024 Monitoring Plugins Development Team
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
 *****************************************************************************/

#include "./maxfd.h"
#include <errno.h>

long mp_open_max(void) {
	long maxfd = 0L;
	/* Try sysconf(_SC_OPEN_MAX) first, as it can be higher than OPEN_MAX.
	 * If that fails and the macro isn't defined, we fall back to an educated
	 * guess. There's no guarantee that our guess is adequate and the program
	 * will die with SIGSEGV if it isn't and the upper boundary is breached. */

#ifdef _SC_OPEN_MAX
	errno = 0;
	if ((maxfd = sysconf(_SC_OPEN_MAX)) < 0) {
		if (errno == 0)
			maxfd = DEFAULT_MAXFD; /* it's indeterminate */
		else
			die(STATE_UNKNOWN, _("sysconf error for _SC_OPEN_MAX\n"));
	}
#elif defined(OPEN_MAX)
	return OPEN_MAX
#else /* sysconf macro unavailable, so guess (may be wildly inaccurate) */
	return DEFAULT_MAXFD;
#endif

	return (maxfd);
}
