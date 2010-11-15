/*****************************************************************************
* 
* Library for check_disk
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains utilities for check_disk. These are tested by libtap
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

/* Initialises a new parameter at the end of list */
struct parameter_list *
np_add_parameter(struct parameter_list **list, const char *name)
{
  struct parameter_list *current = *list;
  struct parameter_list *new_path;
  new_path = (struct parameter_list *) malloc (sizeof *new_path);
  new_path->name = (char *) name;
  new_path->best_match = NULL;
  new_path->name_next = NULL;
  new_path->freespace_bytes = NULL;
  new_path->freespace_units = NULL;
  new_path->freespace_percent = NULL;
  new_path->usedspace_bytes = NULL;
  new_path->usedspace_units = NULL;
  new_path->usedspace_percent = NULL;
  new_path->usedinodes_percent = NULL;
  new_path->freeinodes_percent = NULL;
  new_path->group = NULL;
  new_path->dfree_pct = -1;
  new_path->dused_pct = -1; 
  new_path->total = 0;
  new_path->available = 0;
  new_path->available_to_root = 0;
  new_path->used = 0;
  new_path->dused_units = 0;
  new_path->dfree_units = 0;
  new_path->dtotal_units = 0;
  new_path->inodes_total = 0;
  new_path->inodes_free = 0;
  new_path->dused_inodes_percent = 0;
  new_path->dfree_inodes_percent = 0;

  if (current == NULL) {
    *list = new_path;
  } else {
    while (current->name_next) {
      current = current->name_next;
    }
    current->name_next = new_path;
  }
  return new_path;
}

/* Delete a given parameter from list and return pointer to next element*/
struct parameter_list *
np_del_parameter(struct parameter_list *item, struct parameter_list *prev)
{
  struct parameter_list *next;

  if (item->name_next)
    next = item->name_next;
  else
    next = NULL;

  free(item);
  if (prev)
    prev->name_next = next;

  return next;
}


/* returns a pointer to the struct found in the list */
struct parameter_list *
np_find_parameter(struct parameter_list *list, const char *name)
{
  struct parameter_list *temp_list;
  for (temp_list = list; temp_list; temp_list = temp_list->name_next) {
    if (! strcmp(temp_list->name, name))
        return temp_list;
  }

  return NULL;
}

void
np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, int exact)
{
  struct parameter_list *d;
  for (d = desired; d; d= d->name_next) {
    if (! d->best_match) {
      struct mount_entry *me;
      size_t name_len = strlen(d->name);
      size_t best_match_len = 0;
      struct mount_entry *best_match = NULL;

      /* set best match if path name exactly matches a mounted device name */
      for (me = mount_list; me; me = me->me_next) {
        if (strcmp(me->me_devname, d->name)==0)
          best_match = me;
      }

      /* set best match by directory name if no match was found by devname */
      if (! best_match) {
        for (me = mount_list; me; me = me->me_next) {
          size_t len = strlen (me->me_mountdir);
          if ((exact == FALSE && (best_match_len <= len && len <= name_len &&
             (len == 1 || strncmp (me->me_mountdir, d->name, len) == 0)))
             || (exact == TRUE && strcmp(me->me_mountdir, d->name)==0))
          {
            best_match = me;
            best_match_len = len;
          }
        }
      }

      if (best_match) {
        d->best_match = best_match;
      } else {
        d->best_match = NULL;	/* Not sure why this is needed as it should be null on initialisation */
      }
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

int
np_seen_name(struct name_list *list, const char *name)
{
  const struct name_list *s;
  for (s = list; s; s=s->next) {
    if (!strcmp(s->name, name)) {
      return TRUE;
    }
  }
  return FALSE;
}

int
np_regex_match_mount_entry (struct mount_entry* me, regex_t* re)
{
  if (regexec(re, me->me_devname, (size_t) 0, NULL, 0) == 0 ||
      regexec(re, me->me_mountdir, (size_t) 0, NULL, 0) == 0 ) {
    return TRUE;
  } else {
    return FALSE;
  }
}

