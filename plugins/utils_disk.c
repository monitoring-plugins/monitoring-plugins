/****************************************************************************
* Utils for check_disk
*
* License: GPL
* Copyright (c) 1999-2006 nagios-plugins team
*
* Last Modified: $Date$
*
* Description:
*
* This file contains utilities for check_disk. These are tested by libtap
*
* License Information:
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* $Id$
* 
*****************************************************************************/

#include "common.h"
#include "utils_disk.h"

void
np_add_name (struct name_list **list, const char *name)
{
  struct name_list *new_entry;
  new_entry = (struct name_list *) malloc (sizeof *new_entry);
  new_entry->name = (char *) name;
  new_entry->next = *list;
  *list = new_entry;
}

/* Returns TRUE if name is in list */
int
np_find_name (struct name_list *list, const char *name)
{
  const struct name_list *n;

  if (list == NULL || name == NULL) {
    return FALSE;
  }
  for (n = list; n; n = n->next) {
    if (!strcmp(name, n->name)) {
      return TRUE;
    }
  }
  return FALSE;
}

