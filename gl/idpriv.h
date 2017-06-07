/* Dropping uid/gid privileges of the current process.
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

#ifndef _IDPRIV_H
#define _IDPRIV_H

#ifdef __cplusplus
extern "C" {
#endif

/* This module allows programs which are installed with setuid or setgid bit
   (and which therefore initially run with an effective user id or group id
   different from the one of the current user) to drop their uid or gid
   privilege, either permanently or temporarily.

   It is absolutely necessary to minimize the amount of code that is running
   with escalated privileges (e.g. with effective uid = root). The reason is
   that any bug or exploit in a part of a program that is running with
   escalated privileges is a security vulnerability that - upon discovery -
   puts the users in danger and requires immediate fixing. Then consider that
   there's a bug every 10 or 20 lines of code on average...

   For programs that temporarily drop privileges but have the ability to
   restore them later, there are additionally the dangers that
     - Any bug in the non-privileged part of the program may be used to
       create invalid data structures that will trigger security
       vulnerabilities in the privileged part of the program.
     - Code execution exploits in the non-privileged part of the program may
       be used to invoke the function that restores high privileges and then
       execute additional arbitrary code.

   1) The usual, and reasonably safe, way to minimize the amount of code
      running with privileges is to create a separate executable, with setuid
      or setgid bit, that contains only code for the tasks that require
      privileges (and,of course, strict checking of the arguments, so that the
      program cannot be abused). The main program is installed without setuid
      or setgid bit.

   2) A less safe way is to do some privileged tasks at the beginning of the
      program's run, and drop privileges permanently as soon as possible.

      Note: There may still be security issues if the privileged task puts
      sensitive data into the process memory or opens communication channels
      to restricted facilities.

   3) The most unsafe way is to drop privileges temporarily for most of the
      main program but to re-enable them for the duration of privileged tasks.

      As explained above, this approach has uncontrollable dangers for
      security.

      This approach is normally not usable in multithreaded programs, because
      you cannot know what kind of system calls the other threads could be
      doing during the time the privileges are enabled.

   With approach 1, you don't need gnulib modules.
   With approach 2, you need the gnulib module 'idpriv-drop'.
   With approach 3, you need the gnulib module 'idpriv-droptemp'. But really,
   you should better stay away from this approach.
 */

/* For more in-depth discussion of these topics, see the papers/articles
   * Hao Chen, David Wagner, Drew Dean: Setuid Demystified
     <http://www.usenix.org/events/sec02/full_papers/chen/chen.pdf>
   * Dan Tsafrir, Dilma da Silva, David Wagner: The Murky Issue of Changing
     Process Identity: Revising "Setuid Demystified"
     <http://www.eecs.berkeley.edu/~daw/papers/setuid-login08b.pdf>
     <http://code.google.com/p/change-process-identity/>
   * Dhruv Mohindra: Observe correct revocation order while relinquishing
     privileges
     <https://www.securecoding.cert.org/confluence/display/seccode/POS36-C.+Observe+correct+revocation+order+while+relinquishing+privileges>
 */


/* For approach 2.  */

/* Drop the uid and gid privileges of the current process.
   Return 0 if successful, or -1 with errno set upon failure. The recommended
   handling of failure is to terminate the process.  */
extern int idpriv_drop (void);


/* For approach 3.  */

/* Drop the uid and gid privileges of the current process in a way that allows
   them to be restored later.
   Return 0 if successful, or -1 with errno set upon failure. The recommended
   handling of failure is to terminate the process.  */
extern int idpriv_temp_drop (void);

/* Restore the uid and gid privileges of the current process.
   Return 0 if successful, or -1 with errno set upon failure. The recommended
   handling of failure is to not perform the actions that require the escalated
   privileges.  */
extern int idpriv_temp_restore (void);


#ifdef __cplusplus
}
#endif


#endif /* _IDPRIV_H */
