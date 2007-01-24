/* Substitute for and wrapper around <unistd.h>.
   Copyright (C) 2006 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _GL_UNISTD_H
#define _GL_UNISTD_H

#if HAVE_UNISTD_H
# include @ABSOLUTE_UNISTD_H@
#endif


/* Declare overridden functions.  */

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FCHDIR_REPLACEMENT

/* Change the process' current working directory to the directory on which
   the given file descriptor is open.  */
extern int fchdir (int /*fd*/);

# define close rpl_close
extern int close (int);
# define dup rpl_dup
extern int dup (int);
# define dup2 rpl_dup2
extern int dup2 (int, int);

#endif

#ifdef __cplusplus
}
#endif


#endif /* _GL_UNISTD_H */
