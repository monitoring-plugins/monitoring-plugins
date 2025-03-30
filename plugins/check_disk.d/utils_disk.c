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

#include "../common.h"
#include "utils_disk.h"
#include "../../gl/fsusage.h"
#include "../../lib/thresholds.h"
#include "../../lib/states.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

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

parameter_list_elem parameter_list_init(const char *name) {
	parameter_list_elem result = {
		.name = strdup(name),
		.best_match = NULL,

		.freespace_units = mp_thresholds_init(),
		.freespace_percent = mp_thresholds_init(),
		.freeinodes_percent = mp_thresholds_init(),

		.group = NULL,

		.inodes_total = 0,
		.inodes_free = 0,
		.inodes_free_to_root = 0,
		.inodes_used = 0,

		.used_bytes = 0,
		.free_bytes = 0,
		.total_bytes = 0,

		.next = NULL,
		.prev = NULL,
	};
	return result;
}

/* Returns true if name is in list */
bool np_find_name(struct name_list *list, const char *name) {
	if (list == NULL || name == NULL) {
		return false;
	}
	for (struct name_list *iterator = list; iterator; iterator = iterator->next) {
		if (!strcmp(name, iterator->name)) {
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

	size_t len = strlen(name);

	for (; list; list = list->next) {
		/* Emulate a full match as if surrounded with ^( )$
		   by checking whether the match spans the whole name */
		regmatch_t dummy_match;
		if (!regexec(&list->regex, name, 1, &dummy_match, 0) && dummy_match.rm_so == 0 && dummy_match.rm_eo == len) {
			return true;
		}
	}

	return false;
}

bool np_seen_name(struct name_list *list, const char *name) {
	for (struct name_list *iterator = list; iterator; iterator = iterator->next) {
		if (!strcmp(iterator->name, name)) {
			return true;
		}
	}
	return false;
}

bool np_regex_match_mount_entry(struct mount_entry *me, regex_t *re) {
	return ((regexec(re, me->me_devname, (size_t)0, NULL, 0) == 0) || (regexec(re, me->me_mountdir, (size_t)0, NULL, 0) == 0));
}

check_disk_config check_disk_config_init() {
	check_disk_config tmp = {
		.erronly = false,
		.display_mntp = false,
		.show_local_fs = false,
		.stat_remote_fs = false,
		.display_inodes_perfdata = false,

		.exact_match = false,
		.freespace_ignore_reserved = false,

		.ignore_missing = false,
		.path_ignored = false,

		// FS Filters
		.fs_exclude_list = NULL,
		.fs_include_list = NULL,
		.device_path_exclude_list = NULL,

		// Actual filesystems paths to investigate
		.path_select_list = filesystem_list_init(),

		.mount_list = NULL,
		.seen = NULL,

		.display_unit = Humanized,
		// .unit = MebiBytes,

		.output_format_is_set = false,
	};
	return tmp;
}

char *get_unit_string(byte_unit unit) {
	switch (unit) {
	case Bytes:
		return "Bytes";
	case KibiBytes:
		return "KiB";
	case MebiBytes:
		return "MiB";
	case GibiBytes:
		return "GiB";
	case TebiBytes:
		return "TiB";
	case PebiBytes:
		return "PiB";
	case ExbiBytes:
		return "EiB";
	case KiloBytes:
		return "KB";
	case MegaBytes:
		return "MB";
	case GigaBytes:
		return "GB";
	case TeraBytes:
		return "TB";
	case PetaBytes:
		return "PB";
	case ExaBytes:
		return "EB";
	default:
		assert(false);
	}
}

measurement_unit measurement_unit_init() {
	measurement_unit tmp = {
		.name = NULL,
		.filesystem_type = NULL,
		.is_group = false,

		.freeinodes_percent_thresholds = mp_thresholds_init(),
		.freespace_percent_thresholds = mp_thresholds_init(),
		.freespace_bytes_thresholds = mp_thresholds_init(),

		.free_bytes = 0,
		.used_bytes = 0,
		.total_bytes = 0,

		.inodes_total = 0,
		.inodes_free = 0,
		.inodes_free_to_root = 0,
		.inodes_used = 0,
	};
	return tmp;
}

// Add a given element to the list, memory for the new element is freshly allocated
// Returns a pointer to new element
measurement_unit_list *add_measurement_list(measurement_unit_list *list, measurement_unit elem) {
	// find last element
	measurement_unit_list *new = NULL;
	if (list == NULL) {
		new = calloc(1, sizeof(measurement_unit_list));
		if (new == NULL) {
			die(STATE_UNKNOWN, _("allocation failed"));
		}
	} else {
		measurement_unit_list *list_elem = list;
		while (list_elem->next != NULL) {
			list_elem = list_elem->next;
		}

		new = calloc(1, sizeof(measurement_unit_list));
		if (new == NULL) {
			die(STATE_UNKNOWN, _("allocation failed"));
		}

		list_elem->next = new;
	}

	new->unit = elem;
	new->next = NULL;
	return new;
}

measurement_unit add_filesystem_to_measurement_unit(measurement_unit unit, parameter_list_elem filesystem) {

	unit.free_bytes += filesystem.free_bytes;
	unit.used_bytes += filesystem.used_bytes;
	unit.total_bytes += filesystem.total_bytes;

	unit.inodes_total += filesystem.inodes_total;
	unit.inodes_free += filesystem.inodes_free;
	unit.inodes_free_to_root += filesystem.inodes_free_to_root;
	unit.inodes_used += filesystem.inodes_used;
	return unit;
}

measurement_unit create_measurement_unit_from_filesystem(parameter_list_elem filesystem, bool display_mntp) {
	measurement_unit result = measurement_unit_init();
	if (!display_mntp) {
		result.name = strdup(filesystem.best_match->me_mountdir);
	} else {
		result.name = strdup(filesystem.best_match->me_devname);
	}

	if (filesystem.group) {
		result.is_group = true;
	} else {
		result.is_group = false;
		if (filesystem.best_match) {
			result.filesystem_type = filesystem.best_match->me_type;
		}
	}

	result.freeinodes_percent_thresholds = filesystem.freeinodes_percent;
	result.freespace_percent_thresholds = filesystem.freespace_percent;
	result.freespace_bytes_thresholds = filesystem.freespace_units;
	result.free_bytes = filesystem.free_bytes;
	result.total_bytes = filesystem.total_bytes;
	result.used_bytes = filesystem.used_bytes;
	result.inodes_total = filesystem.inodes_total;
	result.inodes_used = filesystem.inodes_used;
	result.inodes_free = filesystem.inodes_free;
	result.inodes_free_to_root = filesystem.inodes_free_to_root;
	return result;
}

#define RANDOM_STRING_LENGTH 64

char *humanize_byte_value(uintmax_t value, bool use_si_units) {
	// Idea: A reasonable output should have at most 3 orders of magnitude
	// before the decimal separator
	// 353GiB is ok, 2444 GiB should be 2.386 TiB
	char *result = calloc(RANDOM_STRING_LENGTH, sizeof(char));
	if (result == NULL) {
		die(STATE_UNKNOWN, _("allocation failed"));
	}

	if (use_si_units) {
		// SI units, powers of 10
		if (value < KiloBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju B", value);
		} else if (value < MegaBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju KB", value / KiloBytes);
		} else if (value < GigaBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju MB", value / MegaBytes);
		} else if (value < TeraBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju GB", value / GigaBytes);
		} else if (value < PetaBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju TB", value / TeraBytes);
		} else {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju PB", value / PetaBytes);
		}
	} else {
		// IEC units, powers of 2 ^ 10
		if (value < KibiBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju B", value);
		} else if (value < MebiBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju KiB", value / KibiBytes);
		} else if (value < GibiBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju MiB", value / MebiBytes);
		} else if (value < TebiBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju GiB", value / GibiBytes);
		} else if (value < PebiBytes) {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju TiB", value / TebiBytes);
		} else {
			snprintf(result, RANDOM_STRING_LENGTH, "%ju PiB", value / PebiBytes);
		}
	}

	return result;
}

filesystem_list filesystem_list_init() {
	filesystem_list tmp = {
		.length = 0,
		.first = NULL,
	};
	return tmp;
}

parameter_list_elem *mp_int_fs_list_append(filesystem_list *list, const char *name) {
	parameter_list_elem *current = list->first;
	parameter_list_elem *new_path = (struct parameter_list *)malloc(sizeof *new_path);
	*new_path = parameter_list_init(name);

	if (current == NULL) {
		list->first = new_path;
		new_path->prev = NULL;
		list->length = 1;
	} else {
		while (current->next) {
			current = current->next;
		}
		current->next = new_path;
		new_path->prev = current;
		list->length++;
	}
	return new_path;
}

parameter_list_elem *mp_int_fs_list_find(filesystem_list list, const char *name) {
	if (list.length == 0) {
		return NULL;
	}

	for (parameter_list_elem *temp_list = list.first; temp_list; temp_list = temp_list->next) {
		if (!strcmp(temp_list->name, name)) {
			return temp_list;
		}
	}

	return NULL;
}

parameter_list_elem *mp_int_fs_list_del(filesystem_list *list, parameter_list_elem *item) {
	if (list->length == 0) {
		return NULL;
	}

	if (item == NULL) {
		// Got NULL for item, interpret this as "delete first element"
		// as a kind of compatibility to the old function
		item = list->first;
	}

	if (list->first == item) {
		list->length--;

		list->first = item->next;
		if (list->first) {
			list->first->prev = NULL;
		}
		return list->first;
	}

	// Was not the first element, continue
	parameter_list_elem *prev = list->first;
	parameter_list_elem *current = list->first->next;

	while (current != item && current != NULL) {
		prev = current;
		current = current->next;
	}

	if (current == NULL) {
		// didn't find that element ....
		return NULL;
	}

	// remove the element
	parameter_list_elem *next = current->next;
	prev->next = next;
	list->length--;
	if (next) {
		next->prev = prev;
	}

	if (item->name) {
		free(item->name);
	}
	free(item);

	return next;
}

parameter_list_elem *mp_int_fs_list_get_next(parameter_list_elem *current) {
	if (!current) {
		return NULL;
	}
	return current->next;
}

void mp_int_fs_list_set_best_match(filesystem_list list, struct mount_entry *mount_list, bool exact) {
	for (parameter_list_elem *elem = list.first; elem; elem = mp_int_fs_list_get_next(elem)) {
		if (!elem->best_match) {
			size_t name_len = strlen(elem->name);
			struct mount_entry *best_match = NULL;

			/* set best match if path name exactly matches a mounted device name */
			for (struct mount_entry *mount_entry = mount_list; mount_entry; mount_entry = mount_entry->me_next) {
				if (strcmp(mount_entry->me_devname, elem->name) == 0) {
					struct fs_usage fsp;
					if (get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp) >= 0) {
						best_match = mount_entry;
					}
				}
			}

			/* set best match by directory name if no match was found by devname */
			if (!best_match) {
				size_t best_match_len = 0;
				for (struct mount_entry *mount_entry = mount_list; mount_entry; mount_entry = mount_entry->me_next) {
					size_t len = strlen(mount_entry->me_mountdir);

					if ((!exact && (best_match_len <= len && len <= name_len &&
									(len == 1 || strncmp(mount_entry->me_mountdir, elem->name, len) == 0))) ||
						(exact && strcmp(mount_entry->me_mountdir, elem->name) == 0)) {
						struct fs_usage fsp;

						if (get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp) >= 0) {
							best_match = mount_entry;
							best_match_len = len;
						}
					}
				}
			}

			if (best_match) {
				elem->best_match = best_match;
			} else {
				elem->best_match = NULL; /* Not sure why this is needed as it should be null on initialisation */
			}

			// No filesystem without a mount_entry!
			// assert(elem->best_match != NULL);
		}
	}
}
