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

void
np_add_parameter(struct parameter_list **list, const char *name)
{
  struct parameter_list *new_path;
  new_path = (struct parameter_list *) malloc (sizeof *new_path);
  new_path->name = (char *) name;
  new_path->found = 0;
  new_path->found_len = 0;
  new_path->w_df = 0;
  new_path->c_df = 0;
  new_path->w_dfp = -1.0;
  new_path->c_dfp = -1.0;
  new_path->w_idfp = -1.0;
  new_path->c_idfp = -1.0;
  new_path->name_next = *list;
  *list = new_path;
}

void
np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, int exact)
{
  struct parameter_list *d;
  for (d = desired; d; d= d->name_next) {
    struct mount_entry *me;
    size_t name_len = strlen(d->name);
    size_t best_match_len = 0;
    struct mount_entry *best_match = NULL;

    for (me = mount_list; me; me = me->me_next) {
      size_t len = strlen (me->me_mountdir);
      if ((exact == FALSE && (best_match_len <= len && len <= name_len && 
        (len == 1 || strncmp (me->me_mountdir, d->name, len) == 0)))
	|| (exact == TRUE && strcmp(me->me_mountdir, d->name)==0))
      {
        best_match = me;
        best_match_len = len;
      } else {
        len = strlen (me->me_devname);
        if ((exact == FALSE && (best_match_len <= len && len <= name_len &&
          (len == 1 || strncmp (me->me_devname, d->name, len) == 0)))
          || (exact == TRUE && strcmp(me->me_devname, d->name)==0))
        {
          best_match = me;
          best_match_len = len;
        }
      }
    }
    if (best_match) {
      d->best_match = best_match;
      d->found = TRUE;
    }
  }
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

