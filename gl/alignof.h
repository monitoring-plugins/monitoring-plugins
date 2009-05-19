/* Determine alignment of types.
   Copyright (C) 2003-2004, 2006, 2009 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _ALIGNOF_H
#define _ALIGNOF_H

#include <stddef.h>

/* Determine the alignment of a type at compile time.  */
#if defined __GNUC__
# define alignof __alignof__
#elif defined __cplusplus
  template <class type> struct alignof_helper { char __slot1; type __slot2; };
# define alignof(type) offsetof (alignof_helper<type>, __slot2)
#else
# define alignof(type) offsetof (struct { char __slot1; type __slot2; }, __slot2)
#endif

#endif /* _ALIGNOF_H */
