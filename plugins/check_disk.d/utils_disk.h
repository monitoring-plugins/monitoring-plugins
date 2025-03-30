#pragma once
/* Header file for utils_disk */

#include "../../config.h"
#include "../../gl/mountlist.h"
#include "../../lib/utils_base.h"
#include "../../lib/output.h"
#include "regex.h"
#include <stdint.h>

typedef enum : unsigned long {
	Humanized = 0,
	Bytes = 1,
	KibiBytes = 1024,
	MebiBytes = 1024 * KibiBytes,
	GibiBytes = 1024 * MebiBytes,
	TebiBytes = 1024 * GibiBytes,
	PebiBytes = 1024 * TebiBytes,
	ExbiBytes = 1024 * PebiBytes,
	KiloBytes = 1000,
	MegaBytes = 1000 * KiloBytes,
	GigaBytes = 1000 * MegaBytes,
	TeraBytes = 1000 * GigaBytes,
	PetaBytes = 1000 * TeraBytes,
	ExaBytes = 1000 * PetaBytes
} byte_unit;

typedef struct name_list string_list;
struct name_list {
	char *name;
	string_list *next;
};

struct regex_list {
	regex_t regex;
	struct regex_list *next;
};

typedef struct parameter_list parameter_list_elem;
struct parameter_list {
	char *name;
	char *group;

	mp_thresholds freespace_units;
	mp_thresholds freespace_percent;
	mp_thresholds freeinodes_percent;

	struct mount_entry *best_match;

	uintmax_t inodes_free_to_root;
	uintmax_t inodes_free;
	uintmax_t inodes_used;
	uintmax_t inodes_total;

	uint64_t used_bytes;
	uint64_t free_bytes;
	uint64_t total_bytes;

	parameter_list_elem *next;
	parameter_list_elem *prev;
};

typedef struct {
	size_t length;
	parameter_list_elem *first;
} filesystem_list;

filesystem_list filesystem_list_init();

typedef struct {
	char *name;
	char *filesystem_type;
	bool is_group;

	mp_thresholds freespace_bytes_thresholds;
	mp_thresholds freespace_percent_thresholds;
	mp_thresholds freeinodes_percent_thresholds;

	uintmax_t inodes_free_to_root;
	uintmax_t inodes_free;
	uintmax_t inodes_used;
	uintmax_t inodes_total;

	uintmax_t used_bytes;
	uintmax_t free_bytes;
	uintmax_t total_bytes;
} measurement_unit;

typedef struct measurement_unit_list measurement_unit_list;
struct measurement_unit_list {
	measurement_unit unit;
	measurement_unit_list *next;
};

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
	bool freespace_ignore_reserved;

	bool ignore_missing;
	bool path_ignored;

	/* Linked list of filesystem types to omit.
	   If the list is empty, don't exclude any types.  */
	struct regex_list *fs_exclude_list;
	/* Linked list of filesystem types to check.
	   If the list is empty, include all types.  */
	struct regex_list *fs_include_list;
	struct name_list *device_path_exclude_list;
	filesystem_list path_select_list;
	/* Linked list of mounted filesystems. */
	struct mount_entry *mount_list;
	struct name_list *seen;

	byte_unit display_unit;
	// byte_unit unit;

	bool output_format_is_set;
	mp_output_format output_format;
} check_disk_config;

void np_add_name(struct name_list **list, const char *name);
bool np_find_name(struct name_list *list, const char *name);
bool np_seen_name(struct name_list *list, const char *name);
int np_add_regex(struct regex_list **list, const char *regex, int cflags);
bool np_find_regmatch(struct regex_list *list, const char *name);

parameter_list_elem parameter_list_init(const char *);

parameter_list_elem *mp_int_fs_list_append(filesystem_list *list, const char *name);
parameter_list_elem *mp_int_fs_list_find(filesystem_list list, const char *name);
parameter_list_elem *mp_int_fs_list_del(filesystem_list *list, parameter_list_elem *item);
parameter_list_elem *mp_int_fs_list_get_next(parameter_list_elem *current);
void mp_int_fs_list_set_best_match(filesystem_list list, struct mount_entry *mount_list, bool exact);

measurement_unit measurement_unit_init();
measurement_unit_list *add_measurement_list(measurement_unit_list *list, measurement_unit elem);
measurement_unit add_filesystem_to_measurement_unit(measurement_unit unit, parameter_list_elem filesystem);
measurement_unit create_measurement_unit_from_filesystem(parameter_list_elem filesystem, bool display_mntp);

int search_parameter_list(parameter_list_elem *list, const char *name);
bool np_regex_match_mount_entry(struct mount_entry *, regex_t *);

char *get_unit_string(byte_unit);
check_disk_config check_disk_config_init();

char *humanize_byte_value(uintmax_t value, bool use_si_units);
