#include "./maxfd.h"
#include <errno.h>

long mp_open_max (void) {
	long maxfd = 0L;
	/* Try sysconf(_SC_OPEN_MAX) first, as it can be higher than OPEN_MAX.
	 * If that fails and the macro isn't defined, we fall back to an educated
	 * guess. There's no guarantee that our guess is adequate and the program
	 * will die with SIGSEGV if it isn't and the upper boundary is breached. */

#ifdef _SC_OPEN_MAX
	errno = 0;
	if ((maxfd = sysconf (_SC_OPEN_MAX)) < 0) {
		if (errno == 0)
			maxfd = DEFAULT_MAXFD;   /* it's indeterminate */
		else
			die (STATE_UNKNOWN, _("sysconf error for _SC_OPEN_MAX\n"));
	}
#elif defined(OPEN_MAX)
	return OPEN_MAX
#else	/* sysconf macro unavailable, so guess (may be wildly inaccurate) */
	return DEFAULT_MAXFD;
#endif

	return(maxfd);
}
