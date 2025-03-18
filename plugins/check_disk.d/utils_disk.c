/*****************************************************************************
 *
 * Library for check_disk
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
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
#include "gl/fsusage.h"
#include <string.h>

void np_add_name(struct name_list **list, const char *name) {
	struct name_list *new_entry;
	new_entry = (struct name_list *)malloc(sizeof *new_entry);
	new_entry->name = (char *)name;
	new_entry->next = *list;
	*list = new_entry;
}

/* @brief Initialises a new regex at the begin of list via regcomp(3)
 *
 * @details if the regex fails to compile the error code of regcomp(3) is returned
 * 					and list is not modified, otherwise list is modified to point to the new
 * 					element
 * @param list Pointer to a linked list of regex_list elements
 * @param regex the string containing the regex which should be inserted into the list
 * @param clags the cflags parameter for regcomp(3)
 */
int np_add_regex(struct regex_list **list, const char *regex, int cflags) {
	struct regex_list *new_entry = (struct regex_list *)malloc(sizeof *new_entry);

	if (new_entry == NULL) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	int regcomp_result = regcomp(&new_entry->regex, regex, cflags);

	if (!regcomp_result) {
		// regcomp succeeded
		new_entry->next = *list;
		*list = new_entry;

		return 0;
	}
	// regcomp failed
	free(new_entry);

	return regcomp_result;
}

struct parameter_list parameter_list_init(const char *name) {
	struct parameter_list result = {
		.name = strdup(name),
		.best_match = NULL,

		.name_next = NULL,
		.name_prev = NULL,

		.freespace_units = NULL,
		.freespace_percent = NULL,
		.usedspace_units = NULL,
		.usedspace_percent = NULL,
		.usedinodes_percent = NULL,
		.freeinodes_percent = NULL,

		.group = NULL,
		.dfree_pct = -1,
		.dused_pct = -1,
		.total = 0,
		.available = 0,
		.available_to_root = 0,
		.used = 0,
		.dused_units = 0,
		.dfree_units = 0,
		.dtotal_units = 0,
		.inodes_total = 0,
		.inodes_free = 0,
		.inodes_free_to_root = 0,
		.inodes_used = 0,
		.dused_inodes_percent = 0,
		.dfree_inodes_percent = 0,
	};
	return result;
}

/* Initialises a new parameter at the end of list */
struct parameter_list *np_add_parameter(struct parameter_list **list, const char *name) {
	struct parameter_list *current = *list;
	struct parameter_list *new_path;
	new_path = (struct parameter_list *)malloc(sizeof *new_path);

	*new_path = parameter_list_init(name);

	if (current == NULL) {
		*list = new_path;
		new_path->name_prev = NULL;
	} else {
		while (current->name_next) {
			current = current->name_next;
		}
		current->name_next = new_path;
		new_path->name_prev = current;
	}
	return new_path;
}

/* Delete a given parameter from list and return pointer to next element*/
struct parameter_list *np_del_parameter(struct parameter_list *item, struct parameter_list *prev) {
	if (item == NULL) {
		return NULL;
	}

	struct parameter_list *next;

	if (item->name_next) {
		next = item->name_next;
	} else {
		next = NULL;
	}

	if (next) {
		next->name_prev = prev;
	}

	if (prev) {
		prev->name_next = next;
	}

	if (item->name) {
		free(item->name);
	}
	free(item);

	return next;
}

/* returns a pointer to the struct found in the list */
struct parameter_list *np_find_parameter(struct parameter_list *list, const char *name) {
	for (struct parameter_list *temp_list = list; temp_list; temp_list = temp_list->name_next) {
		if (!strcmp(temp_list->name, name)) {
			return temp_list;
		}
	}

	return NULL;
}

void np_set_best_match(struct parameter_list *desired, struct mount_entry *mount_list, bool exact) {
	for (struct parameter_list *d = desired; d; d = d->name_next) {
		if (!d->best_match) {
			struct mount_entry *mount_entry;
			size_t name_len = strlen(d->name);
			size_t best_match_len = 0;
			struct mount_entry *best_match = NULL;
			struct fs_usage fsp;

			/* set best match if path name exactly matches a mounted device name */
			for (mount_entry = mount_list; mount_entry; mount_entry = mount_entry->me_next) {
				if (strcmp(mount_entry->me_devname, d->name) == 0) {
					if (get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp) >= 0) {
						best_match = mount_entry;
					}
				}
			}

			/* set best match by directory name if no match was found by devname */
			if (!best_match) {
				for (mount_entry = mount_list; mount_entry; mount_entry = mount_entry->me_next) {
					size_t len = strlen(mount_entry->me_mountdir);
					if ((!exact && (best_match_len <= len && len <= name_len &&
									(len == 1 || strncmp(mount_entry->me_mountdir, d->name, len) == 0))) ||
						(exact && strcmp(mount_entry->me_mountdir, d->name) == 0)) {
						if (get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp) >= 0) {
							best_match = mount_entry;
							best_match_len = len;
						}
					}
				}
			}

			if (best_match) {
				d->best_match = best_match;
			} else {
				d->best_match = NULL; /* Not sure why this is needed as it should be null on initialisation */
			}
		}
	}
}

/* Returns true if name is in list */
bool np_find_name(struct name_list *list, const char *name) {
	if (list == NULL || name == NULL) {
		return false;
	}
	for (struct name_list *n = list; n; n = n->next) {
		if (!strcmp(name, n->name)) {
			return true;
		}
	}
	return false;
}

/* Returns true if name is in list */
bool np_find_regmatch(struct regex_list *list, const char *name) {
	if (name == NULL) {
		return false;
	}

	int len = strlen(name);

	for (; list; list = list->next) {
		/* Emulate a full match as if surrounded with ^( )$
		   by checking whether the match spans the whole name */
		regmatch_t m;
		if (!regexec(&list->regex, name, 1, &m, 0) && m.rm_so == 0 && m.rm_eo == len) {
			return true;
		}
	}

	return false;
}

bool np_seen_name(struct name_list *list, const char *name) {
	for (struct name_list *s = list; s; s = s->next) {
		if (!strcmp(s->name, name)) {
			return true;
		}
	}
	return false;
}

bool np_regex_match_mount_entry(struct mount_entry *me, regex_t *re) {
	return ((regexec(re, me->me_devname, (size_t)0, NULL, 0) == 0) || (regexec(re, me->me_mountdir, (size_t)0, NULL, 0) == 0));
}
