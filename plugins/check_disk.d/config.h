#pragma once

#include "../../config.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
	// Output options
	bool erronly;
	bool display_mntp;
	/* show only local filesystems.  */
	bool show_local_fs;
	/* show only local filesystems but call stat() on remote ones. */
	bool stat_remote_fs;
	bool display_inodes_perfdata;

	bool exact_match;
	bool ignore_missing;
	bool path_ignored;
	bool path_selected;
	bool freespace_ignore_reserved;

	char *warn_freespace_units;
	char *crit_freespace_units;
	char *warn_freespace_percent;
	char *crit_freespace_percent;
	char *warn_usedspace_units;
	char *crit_usedspace_units;
	char *warn_usedspace_percent;
	char *crit_usedspace_percent;
	char *warn_usedinodes_percent;
	char *crit_usedinodes_percent;
	char *warn_freeinodes_percent;
	char *crit_freeinodes_percent;

	/* Linked list of filesystem types to omit.
	   If the list is empty, don't exclude any types.  */
	struct regex_list *fs_exclude_list;
	/* Linked list of filesystem types to check.
	   If the list is empty, include all types.  */
	struct regex_list *fs_include_list;
	struct name_list *device_path_exclude_list;
	struct parameter_list *path_select_list;
	/* Linked list of mounted filesystems. */
	struct mount_entry *mount_list;
	struct name_list *seen;

	char *units;
	uintmax_t mult;
	char *group;
} check_disk_config;

check_disk_config check_disk_config_init() {
	check_disk_config tmp = {
		.erronly = false,
		.display_mntp = false,
		.show_local_fs = false,
		.stat_remote_fs = false,
		.display_inodes_perfdata = false,

		.exact_match = false,
		.ignore_missing = false,
		.path_ignored = false,
		.path_selected = false,
		.freespace_ignore_reserved = false,

		.warn_freespace_units = NULL,
		.crit_freespace_units = NULL,
		.warn_freespace_percent = NULL,
		.crit_freespace_percent = NULL,
		.warn_usedspace_units = NULL,
		.crit_usedspace_units = NULL,
		.warn_usedspace_percent = NULL,
		.crit_usedspace_percent = NULL,
		.warn_usedinodes_percent = NULL,
		.crit_usedinodes_percent = NULL,
		.warn_freeinodes_percent = NULL,
		.crit_freeinodes_percent = NULL,

		.fs_exclude_list = NULL,
		.fs_include_list = NULL,
		.device_path_exclude_list = NULL,
		.path_select_list = NULL,
		.mount_list = NULL,
		.seen = NULL,

		.units = NULL,
		.mult = 1024 * 1024,
		.group = NULL,
	};
	return tmp;
}
