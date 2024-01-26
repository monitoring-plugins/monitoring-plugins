#ifndef _MAXFD_
#define _MAXFD_

#define DEFAULT_MAXFD  256   /* fallback value if no max open files value is set */
#define MAXFD_LIMIT   8192   /* upper limit of open files */

long mp_open_max (void);

#endif // _MAXFD_
