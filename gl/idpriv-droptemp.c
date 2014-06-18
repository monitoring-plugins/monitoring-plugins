/* Dropping uid/gid privileges of the current process temporarily.
   Copyright (C) 2009-2013 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <config.h>

#include "idpriv.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/* The privileged uid and gid that the process had earlier.  */
#if HAVE_GETUID
static int saved_uid = -1;
#endif
#if HAVE_GETGID
static int saved_gid = -1;
#endif

int
idpriv_temp_drop (void)
{
#if HAVE_GETEUID && HAVE_GETEGID && (HAVE_SETRESUID || HAVE_SETREUID) && (HAVE_SETRESGID || HAVE_SETREGID)
  int uid = getuid ();
  int gid = getgid ();

  /* Find out about the privileged uid and gid at the first call.  */
  if (saved_uid == -1)
    saved_uid = geteuid ();
  if (saved_gid == -1)
    saved_gid = getegid ();

  /* Drop the gid privilege first, because in some cases the gid privilege
     cannot be dropped after the uid privilege has been dropped.  */

  /* This is for executables that have the setgid bit set.  */
# if HAVE_SETRESGID /* glibc, FreeBSD, OpenBSD, HP-UX */
  if (setresgid (-1, gid, saved_gid) < 0)
    return -1;
# else /* Mac OS X, NetBSD, AIX, IRIX, Solaris >= 2.5, OSF/1, Cygwin */
  if (setregid (-1, gid) < 0)
    return -1;
# endif

  /* This is for executables that have the setuid bit set.  */
# if HAVE_SETRESUID /* glibc, FreeBSD, OpenBSD, HP-UX */
  /* See <http://www.usenix.org/events/sec02/full_papers/chen/chen.pdf>
     figure 14.  */
  if (setresuid (-1, uid, saved_uid) < 0)
    return -1;
# else /* Mac OS X, NetBSD, AIX, IRIX, Solaris >= 2.5, OSF/1, Cygwin */
  if (setreuid (-1, uid) < 0)
    return -1;
# endif

  /* Verify that the privileges have really been dropped.
     This verification is here for security reasons.  Doesn't matter if it
     takes a couple of system calls.
     When the verification fails, it indicates that we need to use different
     API in the code above. Therefore 'abort ()', not 'return -1'.  */
# if HAVE_GETRESUID /* glibc, FreeBSD, OpenBSD, HP-UX */
  {
    uid_t real;
    uid_t effective;
    uid_t saved;
    if (getresuid (&real, &effective, &saved) < 0
        || real != uid
        || effective != uid
        || saved != saved_uid)
      abort ();
  }
# else
#  if HAVE_GETEUID
  if (geteuid () != uid)
    abort ();
#  endif
  if (getuid () != uid)
    abort ();
# endif
# if HAVE_GETRESGID /* glibc, FreeBSD, OpenBSD, HP-UX */
  {
    uid_t real;
    uid_t effective;
    uid_t saved;
    if (getresgid (&real, &effective, &saved) < 0
        || real != gid
        || effective != gid
        || saved != saved_gid)
      abort ();
  }
# else
#  if HAVE_GETEGID
  if (getegid () != gid)
    abort ();
#  endif
  if (getgid () != gid)
    abort ();
# endif

  return 0;
#else
  errno = ENOSYS;
  return -1;
#endif
}

int
idpriv_temp_restore (void)
{
#if HAVE_GETEUID && HAVE_GETEGID && (HAVE_SETRESUID || HAVE_SETREUID) && (HAVE_SETRESGID || HAVE_SETREGID)
  int uid = getuid ();
  int gid = getgid ();

  if (saved_uid == -1 || saved_gid == -1)
    /* Caller error: idpriv_temp_drop was never invoked.  */
    abort ();

  /* Acquire the gid privilege last, because in some cases the gid privilege
     cannot be acquired before the uid privilege has been acquired.  */

  /* This is for executables that have the setuid bit set.  */
# if HAVE_SETRESUID /* glibc, FreeBSD, OpenBSD, HP-UX */
  /* See <http://www.usenix.org/events/sec02/full_papers/chen/chen.pdf>
     figure 14.  */
  if (setresuid (-1, saved_uid, -1) < 0)
    return -1;
# else /* Mac OS X, NetBSD, AIX, IRIX, Solaris >= 2.5, OSF/1, Cygwin */
  if (setreuid (-1, saved_uid) < 0)
    return -1;
# endif

  /* This is for executables that have the setgid bit set.  */
# if HAVE_SETRESGID /* glibc, FreeBSD, OpenBSD, HP-UX */
  if (setresgid (-1, saved_gid, -1) < 0)
    return -1;
# else /* Mac OS X, NetBSD, AIX, IRIX, Solaris >= 2.5, OSF/1, Cygwin */
  if (setregid (-1, saved_gid) < 0)
    return -1;
# endif

  /* Verify that the privileges have really been acquired.
     This verification is here for security reasons.  Doesn't matter if it
     takes a couple of system calls.
     When the verification fails, it indicates that we need to use different
     API in the code above. Therefore 'abort ()', not 'return -1'.  */
# if HAVE_GETRESUID /* glibc, FreeBSD, OpenBSD, HP-UX */
  {
    uid_t real;
    uid_t effective;
    uid_t saved;
    if (getresuid (&real, &effective, &saved) < 0
        || real != uid
        || effective != saved_uid
        || saved != saved_uid)
      abort ();
  }
# else
#  if HAVE_GETEUID
  if (geteuid () != saved_uid)
    abort ();
#  endif
  if (getuid () != uid)
    abort ();
# endif
# if HAVE_GETRESGID /* glibc, FreeBSD, OpenBSD, HP-UX */
  {
    uid_t real;
    uid_t effective;
    uid_t saved;
    if (getresgid (&real, &effective, &saved) < 0
        || real != gid
        || effective != saved_gid
        || saved != saved_gid)
      abort ();
  }
# else
#  if HAVE_GETEGID
  if (getegid () != saved_gid)
    abort ();
#  endif
  if (getgid () != gid)
    abort ();
# endif

  return 0;
#else
  errno = ENOSYS;
  return -1;
#endif
}
