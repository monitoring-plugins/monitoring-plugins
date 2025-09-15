/*****************************************************************************
 *
 * Monitoring check_disk plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_disk plugin
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

const char *progname = "check_disk";
const char *program_name = "check_disk"; /* Required for coreutils libs */
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#include "states.h"
#include "common.h"
#include "output.h"
#include "perfdata.h"
#include "utils_base.h"
#include "lib/thresholds.h"

#ifdef HAVE_SYS_STAT_H
#	include <sys/stat.h>
#endif

#if HAVE_INTTYPES_H
#	include <inttypes.h>
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <float.h>
#include "./popen.h"
#include "./utils.h"
#include "../gl/fsusage.h"
#include "../gl/mountlist.h"
#include "./check_disk.d/utils_disk.h"

#if HAVE_LIMITS_H
#	include <limits.h>
#endif

#include "regex.h"

#ifdef __CYGWIN__
#	include <windows.h>
#	undef ERROR
#	define ERROR -1
#endif

#ifdef _AIX
#	pragma alloca
#endif

typedef struct {
	int errorcode;
	check_disk_config config;
} check_disk_config_wrapper;
static check_disk_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void set_all_thresholds(parameter_list_elem *path, char *warn_freespace_units,
							   char *crit_freespace_units, char *warn_freespace_percent,
							   char *crit_freespace_percent, char *warn_freeinodes_percent,
							   char *crit_freeinodes_percent);
static double calculate_percent(uintmax_t /*value*/, uintmax_t /*total*/);
static bool stat_path(parameter_list_elem * /*parameters*/, bool /*ignore_missing*/);

/*
 * Puts the values from a struct fs_usage into a parameter_list with an additional flag to control
 * how reserved and inodes should be judged (ignored or not)
 */
static parameter_list_elem get_path_stats(parameter_list_elem parameters, struct fs_usage fsp,
										  bool freespace_ignore_reserved);
static mp_subcheck evaluate_filesystem(measurement_unit measurement_unit,
									   bool display_inodes_perfdata, byte_unit unit);

void print_usage(void);
static void print_help(void);

static int verbose = 0;

// This would not be necessary in C23!!
const byte_unit Bytes_Factor = 1;
const byte_unit KibiBytes_factor = 1024;
const byte_unit MebiBytes_factor = 1048576;
const byte_unit GibiBytes_factor = 1073741824;
const byte_unit TebiBytes_factor = 1099511627776;
const byte_unit PebiBytes_factor = 1125899906842624;
const byte_unit ExbiBytes_factor = 1152921504606846976;
const byte_unit KiloBytes_factor = 1000;
const byte_unit MegaBytes_factor = 1000000;
const byte_unit GigaBytes_factor = 1000000000;
const byte_unit TeraBytes_factor = 1000000000000;
const byte_unit PetaBytes_factor = 1000000000000000;
const byte_unit ExaBytes_factor = 1000000000000000000;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

#ifdef __CYGWIN__
	char mountdir[32];
#endif

	// Parse extra opts if any
	argv = np_extra_opts(&argc, argv, progname);

	check_disk_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_disk_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	if (config.erronly) {
		mp_set_level_of_detail(MP_DETAIL_NON_OK_ONLY);
	}

	if (!config.path_ignored) {
		mp_int_fs_list_set_best_match(config.path_select_list, config.mount_list,
									  config.exact_match);
	}

	// Error if no match found for specified paths
	for (parameter_list_elem *elem = config.path_select_list.first; elem;) {
		if (!elem->best_match && config.ignore_missing) {
			/* Delete the path from the list so that it is not stat-checked later in the code. */
			elem = mp_int_fs_list_del(&config.path_select_list, elem);
			continue;
		}
		if (!elem->best_match) {
			/* Without --ignore-missing option, exit with Critical state. */
			die(STATE_CRITICAL, _("DISK %s: %s not found\n"), _("CRITICAL"), elem->name);
		}

		elem = mp_int_fs_list_get_next(elem);
	}

	mp_check overall = mp_check_init();
	if (config.path_select_list.length == 0) {
		mp_subcheck none_sc = mp_subcheck_init();
		xasprintf(&none_sc.output, "No filesystems were found for the provided parameters");
		if (config.ignore_missing) {
			none_sc = mp_set_subcheck_state(none_sc, STATE_OK);
		} else {
			none_sc = mp_set_subcheck_state(none_sc, STATE_UNKNOWN);
			if (verbose >= 2) {
				printf("None of the provided paths were found\n");
			}
		}
		mp_add_subcheck_to_check(&overall, none_sc);
		mp_exit(overall);
	}

	// Filter list first
	for (parameter_list_elem *path = config.path_select_list.first; path;) {
		if (!path->best_match) {
			path = mp_int_fs_list_del(&config.path_select_list, path);
			continue;
		}

		struct mount_entry *mount_entry = path->best_match;

#ifdef __CYGWIN__
		if (strncmp(path->name, "/cygdrive/", 10) != 0 || strlen(path->name) > 11) {
			path = mp_int_fs_list_del(&config.path_select_list, path);
			continue;
		}

		char *mountdir = NULL;
		snprintf(mountdir, sizeof(mountdir), "%s:\\", me->me_mountdir + 10);
		if (GetDriveType(mountdir) != DRIVE_FIXED) {
			mount_entry->me_remote = 1;
		}
#endif

		/* Remove filesystems already seen */
		if (np_seen_name(config.seen, mount_entry->me_mountdir)) {
			path = mp_int_fs_list_del(&config.path_select_list, path);
			continue;
		}

		if (path->group == NULL) {
			if (config.fs_exclude_list &&
				np_find_regmatch(config.fs_exclude_list, mount_entry->me_type)) {
				// Skip excluded fs's
				path = mp_int_fs_list_del(&config.path_select_list, path);
				continue;
			}

			if (config.device_path_exclude_list &&
				(np_find_name(config.device_path_exclude_list, mount_entry->me_devname) ||
				 np_find_name(config.device_path_exclude_list, mount_entry->me_mountdir))) {
				// Skip excluded device or mount paths
				path = mp_int_fs_list_del(&config.path_select_list, path);
				continue;
			}

			if (config.fs_include_list &&
				!np_find_regmatch(config.fs_include_list, mount_entry->me_type)) {
				// Skip not included fstypes
				path = mp_int_fs_list_del(&config.path_select_list, path);
				continue;
			}

			/* Skip remote filesystems if we're not interested in them */
			if (mount_entry->me_remote && config.show_local_fs) {
				if (config.stat_remote_fs) {
					// TODO Stat here
					if (!stat_path(path, config.ignore_missing) && config.ignore_missing) {
					}
				}
				continue;
			}

			// TODO why stat here? remove unstatable fs?
			if (!stat_path(path, config.ignore_missing)) {
				// if (config.ignore_missing) {
				// xasprintf(&ignored, "%s %s;", ignored, path->name);
				// }
				// not accessible, remove from list
				path = mp_int_fs_list_del(&config.path_select_list, path);
				continue;
			}
		}

		path = mp_int_fs_list_get_next(path);
	}

	// now get the actual measurements
	for (parameter_list_elem *filesystem = config.path_select_list.first; filesystem;) {
		// Get actual metrics here
		struct mount_entry *mount_entry = filesystem->best_match;
		struct fs_usage fsp = {0};
		get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp);

		if (fsp.fsu_blocks != 0 && strcmp("none", mount_entry->me_mountdir) != 0) {
			*filesystem = get_path_stats(*filesystem, fsp, config.freespace_ignore_reserved);

			if (verbose >= 3) {
				printf("For %s, used_units=%lu free_units=%lu total_units=%lu "
					   "fsp.fsu_blocksize=%lu\n",
					   mount_entry->me_mountdir, filesystem->used_bytes, filesystem->free_bytes,
					   filesystem->total_bytes, fsp.fsu_blocksize);
			}
		} else {
			// failed to retrieve file system data or not mounted?
			filesystem = mp_int_fs_list_del(&config.path_select_list, filesystem);
			continue;
		}
		filesystem = mp_int_fs_list_get_next(filesystem);
	}

	if (verbose > 2) {
		for (parameter_list_elem *filesystem = config.path_select_list.first; filesystem;
			 filesystem = mp_int_fs_list_get_next(filesystem)) {
			assert(filesystem->best_match != NULL);
			if (filesystem->best_match == NULL) {
				printf("Filesystem path %s has no mount_entry!\n", filesystem->name);
			} else {
				// printf("Filesystem path %s has a mount_entry!\n", filesystem->name);
			}
		}
	}

	measurement_unit_list *measurements = NULL;
	measurement_unit_list *current = NULL;
	// create measuring units, because of groups
	for (parameter_list_elem *filesystem = config.path_select_list.first; filesystem;
		 filesystem = mp_int_fs_list_get_next(filesystem)) {
		assert(filesystem->best_match != NULL);

		if (filesystem->group == NULL) {
			// create a measurement unit for the fs
			measurement_unit unit =
				create_measurement_unit_from_filesystem(*filesystem, config.display_mntp);
			if (measurements == NULL) {
				measurements = current = add_measurement_list(NULL, unit);
			} else {
				current = add_measurement_list(measurements, unit);
			}
		} else {
			// Grouped elements are consecutive
			if (measurements == NULL) {
				// first entry
				measurement_unit unit =
					create_measurement_unit_from_filesystem(*filesystem, config.display_mntp);
				unit.name = strdup(filesystem->group);
				measurements = current = add_measurement_list(NULL, unit);
			} else {
				// if this is the first element of a group, the name of the previous entry is
				// different
				if (strcmp(filesystem->group, current->unit.name) != 0) {
					// so, this must be the first element of a group
					measurement_unit unit =
						create_measurement_unit_from_filesystem(*filesystem, config.display_mntp);
					unit.name = filesystem->group;
					current = add_measurement_list(measurements, unit);

				} else {
					// NOT the first entry of a group, add info to the other one
					current->unit = add_filesystem_to_measurement_unit(current->unit, *filesystem);
				}
			}
		}
	}

	/* Process for every path in list */
	if (measurements != NULL) {
		for (measurement_unit_list *unit = measurements; unit; unit = unit->next) {
			mp_subcheck unit_sc = evaluate_filesystem(unit->unit, config.display_inodes_perfdata,
													  config.display_unit);
			mp_add_subcheck_to_check(&overall, unit_sc);
		}
	} else {
		// Apparently no machting fs found
		mp_subcheck none_sc = mp_subcheck_init();
		xasprintf(&none_sc.output, "No filesystems were found for the provided parameters");

		if (config.ignore_missing) {
			none_sc = mp_set_subcheck_state(none_sc, STATE_OK);
		} else {
			none_sc = mp_set_subcheck_state(none_sc, STATE_UNKNOWN);
		}
		mp_add_subcheck_to_check(&overall, none_sc);
	}

	mp_exit(overall);
}

double calculate_percent(uintmax_t value, uintmax_t total) {
	double pct = -1;
	if (value <= DBL_MAX && total != 0) {
		pct = (double)value / (double)total * 100.0;
	}

	return pct;
}

/* process command-line arguments */
check_disk_config_wrapper process_arguments(int argc, char **argv) {

	check_disk_config_wrapper result = {
		.errorcode = OK,
		.config = check_disk_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	enum {
		output_format_index = CHAR_MAX + 1,
		display_unit_index,
	};

	static struct option longopts[] = {{"timeout", required_argument, 0, 't'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"iwarning", required_argument, 0, 'W'},
									   {"icritical", required_argument, 0, 'K'},
									   {"kilobytes", no_argument, 0, 'k'},
									   {"megabytes", no_argument, 0, 'm'},
									   {"units", required_argument, 0, 'u'},
									   {"path", required_argument, 0, 'p'},
									   {"partition", required_argument, 0, 'p'},
									   {"exclude_device", required_argument, 0, 'x'},
									   {"exclude-type", required_argument, 0, 'X'},
									   {"include-type", required_argument, 0, 'N'},
									   {"group", required_argument, 0, 'g'},
									   {"eregi-path", required_argument, 0, 'R'},
									   {"eregi-partition", required_argument, 0, 'R'},
									   {"ereg-path", required_argument, 0, 'r'},
									   {"ereg-partition", required_argument, 0, 'r'},
									   {"freespace-ignore-reserved", no_argument, 0, 'f'},
									   {"ignore-ereg-path", required_argument, 0, 'i'},
									   {"ignore-ereg-partition", required_argument, 0, 'i'},
									   {"ignore-eregi-path", required_argument, 0, 'I'},
									   {"ignore-eregi-partition", required_argument, 0, 'I'},
									   {"ignore-missing", no_argument, 0, 'n'},
									   {"local", no_argument, 0, 'l'},
									   {"stat-remote-fs", no_argument, 0, 'L'},
									   {"iperfdata", no_argument, 0, 'P'},
									   {"mountpoint", no_argument, 0, 'M'},
									   {"errors-only", no_argument, 0, 'e'},
									   {"exact-match", no_argument, 0, 'E'},
									   {"all", no_argument, 0, 'A'},
									   {"verbose", no_argument, 0, 'v'},
									   {"quiet", no_argument, 0, 'q'},
									   {"clear", no_argument, 0, 'C'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"output-format", required_argument, 0, output_format_index},
									   {"display-unit", required_argument, 0, display_unit_index},
									   {0, 0, 0, 0}};

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
	}

	int cflags = REG_NOSUB | REG_EXTENDED;
	int default_cflags = cflags;
	char *warn_freespace_units = NULL;
	char *crit_freespace_units = NULL;
	char *warn_freespace_percent = NULL;
	char *crit_freespace_percent = NULL;
	char *warn_freeinodes_percent = NULL;
	char *crit_freeinodes_percent = NULL;

	bool path_selected = false;
	char *group = NULL;
	byte_unit unit = MebiBytes_factor;

	result.config.mount_list = read_file_system_list(false);

	np_add_regex(&result.config.fs_exclude_list, "iso9660", REG_EXTENDED);

	while (true) {
		int option = 0;
		int option_index = getopt_long(
			argc, argv, "+?VqhvefCt:c:w:K:W:u:p:x:X:N:mklLPg:R:r:i:I:MEAn", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case 't': /* timeout period */
			if (is_integer(optarg)) {
				timeout_interval = atoi(optarg);
				break;
			} else {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			}

		/* See comments for 'c' */
		case 'w': /* warning threshold */
			if (!is_percentage_expression(optarg) && !is_numeric(optarg)) {
				die(STATE_UNKNOWN, "Argument for --warning invalid or missing: %s\n", optarg);
			}

			if (strstr(optarg, "%")) {
				if (*optarg == '@') {
					warn_freespace_percent = optarg;
				} else {
					xasprintf(&warn_freespace_percent, "@%s", optarg);
				}
			} else {
				if (*optarg == '@') {
					warn_freespace_units = optarg;
				} else {
					xasprintf(&warn_freespace_units, "@%s", optarg);
				}
			}
			break;

		/* Awful mistake where the range values do not make sense. Normally,
		 * you alert if the value is within the range, but since we are using
		 * freespace, we have to alert if outside the range. Thus we artificially
		 * force @ at the beginning of the range, so that it is backwards compatible
		 */
		case 'c': /* critical threshold */
			if (!is_percentage_expression(optarg) && !is_numeric(optarg)) {
				die(STATE_UNKNOWN, "Argument for --critical invalid or missing: %s\n", optarg);
			}

			if (strstr(optarg, "%")) {
				if (*optarg == '@') {
					crit_freespace_percent = optarg;
				} else {
					xasprintf(&crit_freespace_percent, "@%s", optarg);
				}
			} else {
				if (*optarg == '@') {
					crit_freespace_units = optarg;
				} else {
					xasprintf(&crit_freespace_units, "@%s", optarg);
				}
			}
			break;

		case 'W': /* warning inode threshold */
			if (*optarg == '@') {
				warn_freeinodes_percent = optarg;
			} else {
				xasprintf(&warn_freeinodes_percent, "@%s", optarg);
			}
			break;
		case 'K': /* critical inode threshold */
			if (*optarg == '@') {
				crit_freeinodes_percent = optarg;
			} else {
				xasprintf(&crit_freeinodes_percent, "@%s", optarg);
			}
			break;
		case 'u':
			if (!strcasecmp(optarg, "bytes")) {
				unit = Bytes_Factor;
			} else if (!strcmp(optarg, "KiB")) {
				unit = KibiBytes_factor;
			} else if (!strcmp(optarg, "kB")) {
				unit = KiloBytes_factor;
			} else if (!strcmp(optarg, "MiB")) {
				unit = MebiBytes_factor;
			} else if (!strcmp(optarg, "MB")) {
				unit = MegaBytes_factor;
			} else if (!strcmp(optarg, "GiB")) {
				unit = GibiBytes_factor;
			} else if (!strcmp(optarg, "GB")) {
				unit = GigaBytes_factor;
			} else if (!strcmp(optarg, "TiB")) {
				unit = TebiBytes_factor;
			} else if (!strcmp(optarg, "TB")) {
				unit = TeraBytes_factor;
			} else if (!strcmp(optarg, "PiB")) {
				unit = PebiBytes_factor;
			} else if (!strcmp(optarg, "PB")) {
				unit = PetaBytes_factor;
			} else {
				die(STATE_UNKNOWN, _("unit type %s not known\n"), optarg);
			}
			break;
		case 'k':
			unit = KibiBytes_factor;
			break;
		case 'm':
			unit = MebiBytes_factor;
			break;
		case display_unit_index:
			if (!strcasecmp(optarg, "bytes")) {
				result.config.display_unit = Bytes;
			} else if (!strcmp(optarg, "KiB")) {
				result.config.display_unit = KibiBytes;
			} else if (!strcmp(optarg, "kB")) {
				result.config.display_unit = KiloBytes;
			} else if (!strcmp(optarg, "MiB")) {
				result.config.display_unit = MebiBytes;
			} else if (!strcmp(optarg, "MB")) {
				result.config.display_unit = MegaBytes;
			} else if (!strcmp(optarg, "GiB")) {
				result.config.display_unit = GibiBytes;
			} else if (!strcmp(optarg, "GB")) {
				result.config.display_unit = GigaBytes;
			} else if (!strcmp(optarg, "TiB")) {
				result.config.display_unit = TebiBytes;
			} else if (!strcmp(optarg, "TB")) {
				result.config.display_unit = TeraBytes;
			} else if (!strcmp(optarg, "PiB")) {
				result.config.display_unit = PebiBytes;
			} else if (!strcmp(optarg, "PB")) {
				result.config.display_unit = PetaBytes;
			} else {
				die(STATE_UNKNOWN, _("unit type %s not known\n"), optarg);
			}
			break;
		case 'L':
			result.config.stat_remote_fs = true;
			/* fallthrough */
		case 'l':
			result.config.show_local_fs = true;
			break;
		case 'P':
			result.config.display_inodes_perfdata = true;
			break;
		case 'p': /* select path */ {
			if (!(warn_freespace_units || crit_freespace_units || warn_freespace_percent ||
				  crit_freespace_percent || warn_freeinodes_percent || crit_freeinodes_percent)) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"),
					_("Must set a threshold value before using -p\n"));
			}

			/* add parameter if not found. overwrite thresholds if path has already been added  */
			parameter_list_elem *search_entry;
			if (!(search_entry = mp_int_fs_list_find(result.config.path_select_list, optarg))) {
				search_entry = mp_int_fs_list_append(&result.config.path_select_list, optarg);

				// struct stat stat_buf = {};
				// if (stat(optarg, &stat_buf) && result.config.ignore_missing) {
				// result.config.path_ignored = true;
				// break;
				// }
			}
			search_entry->group = group;
			set_all_thresholds(search_entry, warn_freespace_units, crit_freespace_units,
							   warn_freespace_percent, crit_freespace_percent,

							   warn_freeinodes_percent, crit_freeinodes_percent);

			/* With autofs, it is required to stat() the path before re-populating the mount_list */
			// if (!stat_path(se, result.config.ignore_missing)) {
			// break;
			// }
			mp_int_fs_list_set_best_match(result.config.path_select_list, result.config.mount_list,
										  result.config.exact_match);

			path_selected = true;
		} break;
		case 'x': /* exclude path or partition */
			np_add_name(&result.config.device_path_exclude_list, optarg);
			break;
		case 'X': /* exclude file system type */ {
			int err = np_add_regex(&result.config.fs_exclude_list, optarg, REG_EXTENDED);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &result.config.fs_exclude_list->regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"),
					_("Could not compile regular expression"), errbuf);
			}
			break;
		case 'N': /* include file system type */
			err = np_add_regex(&result.config.fs_include_list, optarg, REG_EXTENDED);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &result.config.fs_exclude_list->regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"),
					_("Could not compile regular expression"), errbuf);
			}
		} break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'q': /* TODO: this function should eventually go away (removed 2007-09-20) */
			/* verbose--; **replaced by line below**. -q was only a broken way of implementing -e */
			result.config.erronly = true;
			break;
		case 'e':
			result.config.erronly = true;
			break;
		case 'E':
			if (path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"),
					_("Must set -E before selecting paths\n"));
			}
			result.config.exact_match = true;
			break;
		case 'f':
			result.config.freespace_ignore_reserved = true;
			break;
		case 'g':
			if (path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"),
					_("Must set group value before selecting paths\n"));
			}
			group = optarg;
			break;
		case 'I':
			cflags |= REG_ICASE;
			// Intentional fallthrough
		case 'i': {
			if (!path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s\n", _("UNKNOWN"),
					_("Paths need to be selected before using -i/-I. Use -A to select all paths "
					  "explicitly"));
			}
			regex_t regex;
			int err = regcomp(&regex, optarg, cflags);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"),
					_("Could not compile regular expression"), errbuf);
			}

			for (parameter_list_elem *elem = result.config.path_select_list.first; elem;) {
				if (elem->best_match) {
					if (np_regex_match_mount_entry(elem->best_match, &regex)) {

						if (verbose >= 3) {
							printf("ignoring %s matching regex\n", elem->name);
						}

						elem = mp_int_fs_list_del(&result.config.path_select_list, elem);
						continue;
					}
				}

				elem = mp_int_fs_list_get_next(elem);
			}

			cflags = default_cflags;
		} break;
		case 'n':
			result.config.ignore_missing = true;
			break;
		case 'A':
			optarg = strdup(".*");
			// Intentional fallthrough
		case 'R':
			cflags |= REG_ICASE;
			// Intentional fallthrough
		case 'r': {
			if (!(warn_freespace_units || crit_freespace_units || warn_freespace_percent ||
				  crit_freespace_percent || warn_freeinodes_percent || crit_freeinodes_percent)) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"),
					_("Must set a threshold value before using -r/-R/-A "
					  "(--ereg-path/--eregi-path/--all)\n"));
			}

			regex_t regex;
			int err = regcomp(&regex, optarg, cflags);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"),
					_("Could not compile regular expression"), errbuf);
			}

			bool found = false;
			for (struct mount_entry *me = result.config.mount_list; me; me = me->me_next) {
				if (np_regex_match_mount_entry(me, &regex)) {
					found = true;
					if (verbose >= 3) {
						printf("%s %s matching expression %s\n", me->me_devname, me->me_mountdir,
							   optarg);
					}

					/* add parameter if not found. overwrite thresholds if path has already been
					 * added  */
					parameter_list_elem *se = NULL;
					if (!(se = mp_int_fs_list_find(result.config.path_select_list,
												   me->me_mountdir))) {
						se =
							mp_int_fs_list_append(&result.config.path_select_list, me->me_mountdir);
					}
					se->group = group;
					set_all_thresholds(se, warn_freespace_units, crit_freespace_units,
									   warn_freespace_percent, crit_freespace_percent,
									   warn_freeinodes_percent, crit_freeinodes_percent);
				}
			}

			if (!found) {
				if (result.config.ignore_missing) {
					result.config.path_ignored = true;
					path_selected = true;
					break;
				}

				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"),
					_("Regular expression did not match any path or disk"), optarg);
			}

			path_selected = true;
			mp_int_fs_list_set_best_match(result.config.path_select_list, result.config.mount_list,
										  result.config.exact_match);
			cflags = default_cflags;

		} break;
		case 'M': /* display mountpoint */
			result.config.display_mntp = true;
			break;
		case 'C': {
			/* add all mount entries to path_select list if no partitions have been explicitly
			 * defined using -p */
			if (!path_selected) {
				parameter_list_elem *path;
				for (struct mount_entry *me = result.config.mount_list; me; me = me->me_next) {
					if (!(path = mp_int_fs_list_find(result.config.path_select_list,
													 me->me_mountdir))) {
						path =
							mp_int_fs_list_append(&result.config.path_select_list, me->me_mountdir);
					}
					path->best_match = me;
					path->group = group;
					set_all_thresholds(path, warn_freespace_units, crit_freespace_units,
									   warn_freespace_percent, crit_freespace_percent,
									   warn_freeinodes_percent, crit_freeinodes_percent);
				}
			}

			warn_freespace_units = NULL;
			crit_freespace_units = NULL;
			warn_freespace_percent = NULL;
			crit_freespace_percent = NULL;
			warn_freeinodes_percent = NULL;
			crit_freeinodes_percent = NULL;

			path_selected = false;
			group = NULL;
		} break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case '?': /* help */
			usage(_("Unknown argument"));
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		}
	}

	/* Support for "check_disk warn crit [fs]" with thresholds at used% level */
	int index = optind;

	if (argc > index && is_intnonneg(argv[index])) {
		if (verbose > 0) {
			printf("Got an positional warn threshold: %s\n", argv[index]);
		}
		char *range = argv[index++];
		mp_range_parsed tmp = mp_parse_range_string(range);
		if (tmp.error != MP_PARSING_SUCCES) {
			die(STATE_UNKNOWN, "failed to parse warning threshold");
		}

		mp_range tmp_range = tmp.range;
		// Invert range to use it for free instead of used
		// tmp_range.alert_on_inside_range = !tmp_range.alert_on_inside_range;

		warn_freespace_percent = mp_range_to_string(tmp_range);

		if (verbose > 0) {
			printf("Positional warning threshold transformed to: %s\n", warn_freespace_percent);
		}
	}

	if (argc > index && is_intnonneg(argv[index])) {
		if (verbose > 0) {
			printf("Got an positional crit threshold: %s\n", argv[index]);
		}
		char *range = argv[index++];
		mp_range_parsed tmp = mp_parse_range_string(range);
		if (tmp.error != MP_PARSING_SUCCES) {
			die(STATE_UNKNOWN, "failed to parse warning threshold");
		}

		mp_range tmp_range = tmp.range;
		// Invert range to use it for free instead of used
		// tmp_range.alert_on_inside_range = !tmp_range.alert_on_inside_range;

		crit_freespace_percent = mp_range_to_string(tmp_range);

		if (verbose > 0) {
			printf("Positional critical threshold transformed to: %s\n", crit_freespace_percent);
		}
	}

	if (argc > index) {
		if (verbose > 0) {
			printf("Got an positional filesystem: %s\n", argv[index]);
		}
		struct parameter_list *se =
			mp_int_fs_list_append(&result.config.path_select_list, strdup(argv[index++]));
		path_selected = true;
		set_all_thresholds(se, warn_freespace_units, crit_freespace_units, warn_freespace_percent,
						   crit_freespace_percent, warn_freeinodes_percent,
						   crit_freeinodes_percent);
	}

	// If a list of paths has not been explicitly selected, find entire
	// mount list and create list of paths
	if (!path_selected && !result.config.path_ignored) {
		for (struct mount_entry *me = result.config.mount_list; me; me = me->me_next) {
			if (me->me_dummy != 0) {
				// just do not add dummy filesystems
				continue;
			}

			parameter_list_elem *path = NULL;
			if (!(path = mp_int_fs_list_find(result.config.path_select_list, me->me_mountdir))) {
				path = mp_int_fs_list_append(&result.config.path_select_list, me->me_mountdir);
			}
			path->best_match = me;
			path->group = group;
			set_all_thresholds(path, warn_freespace_units, crit_freespace_units,
							   warn_freespace_percent, crit_freespace_percent,
							   warn_freeinodes_percent, crit_freeinodes_percent);
		}
	}

	// Set thresholds to the appropriate unit
	for (parameter_list_elem *tmp = result.config.path_select_list.first; tmp;
		 tmp = mp_int_fs_list_get_next(tmp)) {

		mp_perfdata_value factor = mp_create_pd_value(unit);

		if (tmp->freespace_units.critical_is_set) {
			tmp->freespace_units.critical =
				mp_range_multiply(tmp->freespace_units.critical, factor);
		}
		if (tmp->freespace_units.warning_is_set) {
			tmp->freespace_units.warning = mp_range_multiply(tmp->freespace_units.warning, factor);
		}
	}

	return result;
}

void set_all_thresholds(parameter_list_elem *path, char *warn_freespace_units,
						char *crit_freespace_units, char *warn_freespace_percent,
						char *crit_freespace_percent, char *warn_freeinodes_percent,
						char *crit_freeinodes_percent) {
	mp_range_parsed tmp;

	if (warn_freespace_units) {
		tmp = mp_parse_range_string(warn_freespace_units);
		path->freespace_units = mp_thresholds_set_warn(path->freespace_units, tmp.range);
	}

	if (crit_freespace_units) {
		tmp = mp_parse_range_string(crit_freespace_units);
		path->freespace_units = mp_thresholds_set_crit(path->freespace_units, tmp.range);
	}

	if (warn_freespace_percent) {
		tmp = mp_parse_range_string(warn_freespace_percent);
		path->freespace_percent = mp_thresholds_set_warn(path->freespace_percent, tmp.range);
	}

	if (crit_freespace_percent) {
		tmp = mp_parse_range_string(crit_freespace_percent);
		path->freespace_percent = mp_thresholds_set_crit(path->freespace_percent, tmp.range);
	}

	if (warn_freeinodes_percent) {
		tmp = mp_parse_range_string(warn_freeinodes_percent);
		path->freeinodes_percent = mp_thresholds_set_warn(path->freeinodes_percent, tmp.range);
	}

	if (crit_freeinodes_percent) {
		tmp = mp_parse_range_string(crit_freeinodes_percent);
		path->freeinodes_percent = mp_thresholds_set_crit(path->freeinodes_percent, tmp.range);
	}
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin checks the amount of used disk space on a mounted file system"));
	printf("%s\n",
		   _("and generates an alert if free space is less than one of the threshold values"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-w, --warning=INTEGER");
	printf("    %s\n", _("Exit with WARNING status if less than INTEGER units of disk are free"));
	printf(" %s\n", "-w, --warning=PERCENT%");
	printf("    %s\n", _("Exit with WARNING status if less than PERCENT of disk space is free"));
	printf(" %s\n", "-c, --critical=INTEGER");
	printf("    %s\n", _("Exit with CRITICAL status if less than INTEGER units of disk are free"));
	printf(" %s\n", "-c, --critical=PERCENT%");
	printf("    %s\n", _("Exit with CRITICAL status if less than PERCENT of disk space is free"));
	printf(" %s\n", "-W, --iwarning=PERCENT%");
	printf("    %s\n", _("Exit with WARNING status if less than PERCENT of inode space is free"));
	printf(" %s\n", "-K, --icritical=PERCENT%");
	printf("    %s\n", _("Exit with CRITICAL status if less than PERCENT of inode space is free"));
	printf(" %s\n", "-p, --path=PATH, --partition=PARTITION");
	printf("    %s\n",
		   _("Mount point or block device as emitted by the mount(8) command (may be repeated)"));
	printf(" %s\n", "-x, --exclude_device=PATH <STRING>");
	printf("    %s\n", _("Ignore device (only works if -p unspecified)"));
	printf(" %s\n", "-C, --clear");
	printf("    %s\n", _("Clear thresholds"));
	printf(" %s\n", "-E, --exact-match");
	printf("    %s\n", _("For paths or partitions specified with -p, only check for exact paths"));
	printf(" %s\n", "-e, --errors-only");
	printf("    %s\n", _("Display only devices/mountpoints with errors"));
	printf(" %s\n", "-f, --freespace-ignore-reserved");
	printf("    %s\n", _("Don't account root-reserved blocks into freespace in perfdata"));
	printf(" %s\n", "-P, --iperfdata");
	printf("    %s\n", _("Display inode usage in perfdata"));
	printf(" %s\n", "-g, --group=NAME");
	printf("    %s\n",
		   _("Group paths. Thresholds apply to (free-)space of all partitions together"));
	printf(" %s\n", "-l, --local");
	printf("    %s\n", _("Only check local filesystems"));
	printf(" %s\n", "-L, --stat-remote-fs");
	printf(
		"    %s\n",
		_("Only check local filesystems against thresholds. Yet call stat on remote filesystems"));
	printf("    %s\n", _("to test if they are accessible (e.g. to detect Stale NFS Handles)"));
	printf(" %s\n", "-M, --mountpoint");
	printf("    %s\n", _("Display the (block) device instead of the mount point"));
	printf(" %s\n", "-A, --all");
	printf("    %s\n", _("Explicitly select all paths. This is equivalent to -R '.*'"));
	printf(" %s\n", "-R, --eregi-path=PATH, --eregi-partition=PARTITION");
	printf("    %s\n",
		   _("Case insensitive regular expression for path/partition (may be repeated)"));
	printf(" %s\n", "-r, --ereg-path=PATH, --ereg-partition=PARTITION");
	printf("    %s\n", _("Regular expression for path or partition (may be repeated)"));
	printf(" %s\n", "-I, --ignore-eregi-path=PATH, --ignore-eregi-partition=PARTITION");
	printf("    %s\n", _("Regular expression to ignore selected path/partition (case insensitive) "
						 "(may be repeated)"));
	printf(" %s\n", "-i, --ignore-ereg-path=PATH, --ignore-ereg-partition=PARTITION");
	printf("    %s\n",
		   _("Regular expression to ignore selected path or partition (may be repeated)"));
	printf(" %s\n", "-n, --ignore-missing");
	printf("    %s\n",
		   _("Return OK if no filesystem matches, filesystem does not exist or is inaccessible."));
	printf("    %s\n", _("(Provide this option before -p / -r / --ereg-path if used)"));
	printf(UT_PLUG_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf(" %s\n", "-u, --units=STRING");
	printf("    %s\n", _("Select the unit used for the absolute value thresholds"));
	printf("    %s\n", _("Choose one of \"bytes\", \"KiB\", \"kB\", \"MiB\", \"MB\", \"GiB\", "
						 "\"GB\", \"TiB\", \"TB\", \"PiB\", \"PB\" (default: MiB)"));
	printf(" %s\n", "-k, --kilobytes");
	printf("    %s\n", _("Same as '--units kB'"));
	printf(" %s\n", "--display-unit");
	printf("    %s\n", _("Select the unit used for in the output"));
	printf("    %s\n", _("Choose one of \"bytes\", \"KiB\", \"kB\", \"MiB\", \"MB\", \"GiB\", "
						 "\"GB\", \"TiB\", \"TB\", \"PiB\", \"PB\" (default: MiB)"));
	printf(" %s\n", "-m, --megabytes");
	printf("    %s\n", _("Same as '--units MB'"));
	printf(UT_VERBOSE);
	printf(" %s\n", "-X, --exclude-type=TYPE_REGEX");
	printf("    %s\n",
		   _("Ignore all filesystems of types matching given regex(7) (may be repeated)"));
	printf(" %s\n", "-N, --include-type=TYPE_REGEX");
	printf(
		"    %s\n",
		_("Check only filesystems where the type matches this given regex(7) (may be repeated)"));
	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf("%s\n", _("General usage hints:"));
	printf(
		" %s\n",
		_("- Arguments are positional! \"-w 5 -c 1 -p /foo -w6 -c2 -p /bar\" is not the same as"));
	printf("   %s\n", _("\"-w 5 -c 1 -p /bar w6 -c2 -p /foo\"."));
	printf(" %s\n", _("- The syntax is broadly: \"{thresholds a} {paths a} -C {thresholds b} "
					  "{thresholds b} ...\""));

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_disk -w 10% -c 5% -p /tmp -p /var -C -w 100000 -c 50000 -p /");
	printf("    %s\n\n", _("Checks /tmp and /var at 10% and 5%, and / at 100MB and 50MB"));
	printf(" %s\n",
		   "check_disk -w 100 -c 50 -C -w 1000 -c 500 -g sidDATA -r '^/oracle/SID/data.*$'");
	printf(
		"    %s\n",
		_("Checks all filesystems not matching -r at 100M and 50M. The fs matching the -r regex"));
	printf("    %s\n\n",
		   _("are grouped which means the freespace thresholds are applied to all disks together"));
	printf(" %s\n", "check_disk -w 100 -c 50 -C -w 1000 -c 500 -p /foo -C -w 5% -c 3% -p /bar");
	printf("    %s\n",
		   _("Checks /foo for 1000M/500M and /bar for 5/3%. All remaining volumes use 100M/50M"));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s {-w absolute_limit |-w  percentage_limit%% | -W inode_percentage_limit } {-c "
		   "absolute_limit|-c percentage_limit%% | -K "
		   "inode_percentage_limit } {-p path | -x device}\n",
		   progname);
	printf("[-C] [-E] [-e] [-f] [-g group ] [-k] [-l] [-M] [-m] [-R path ] [-r path ]\n");
	printf("[-t timeout] [-u unit] [-v] [-X type_regex] [-N type]\n");
}

bool stat_path(parameter_list_elem *parameters, bool ignore_missing) {
	/* Stat entry to check that dir exists and is accessible */
	if (verbose >= 3) {
		printf("calling stat on %s\n", parameters->name);
	}

	struct stat stat_buf = {0};
	if (stat(parameters->name, &stat_buf)) {
		if (verbose >= 3) {
			printf("stat failed on %s\n", parameters->name);
		}
		if (ignore_missing) {
			return false;
		}
		printf("DISK %s - ", _("CRITICAL"));
		die(STATE_CRITICAL, _("%s %s: %s\n"), parameters->name, _("is not accessible"),
			strerror(errno));
	}

	return true;
}

static parameter_list_elem get_path_stats(parameter_list_elem parameters, const struct fs_usage fsp,
										  bool freespace_ignore_reserved) {
	uintmax_t available = fsp.fsu_bavail;
	uintmax_t available_to_root = fsp.fsu_bfree;
	uintmax_t used = fsp.fsu_blocks - fsp.fsu_bfree;
	uintmax_t total;

	if (freespace_ignore_reserved) {
		/* option activated : we subtract the root-reserved space from the total */
		total = fsp.fsu_blocks - available_to_root + available;
	} else {
		/* default behaviour : take all the blocks into account */
		total = fsp.fsu_blocks;
	}

	parameters.used_bytes = used * fsp.fsu_blocksize;
	parameters.free_bytes = available * fsp.fsu_blocksize;
	parameters.total_bytes = total * fsp.fsu_blocksize;

	/* Free file nodes. Not sure the workaround is required, but in case...*/
	parameters.inodes_free = fsp.fsu_ffree;
	parameters.inodes_free_to_root = fsp.fsu_ffree; /* Free file nodes for root. */
	parameters.inodes_used = fsp.fsu_files - fsp.fsu_ffree;

	if (freespace_ignore_reserved) {
		/* option activated : we subtract the root-reserved inodes from the total */
		/* not all OS report fsp->fsu_favail, only the ones with statvfs syscall */
		/* for others, fsp->fsu_ffree == fsp->fsu_favail */
		parameters.inodes_total =
			fsp.fsu_files - parameters.inodes_free_to_root + parameters.inodes_free;
	} else {
		/* default behaviour : take all the inodes into account */
		parameters.inodes_total = fsp.fsu_files;
	}

	return parameters;
}

mp_subcheck evaluate_filesystem(measurement_unit measurement_unit, bool display_inodes_perfdata,
								byte_unit unit) {
	mp_subcheck result = mp_subcheck_init();
	result = mp_set_subcheck_default_state(result, STATE_UNKNOWN);
	xasprintf(&result.output, "%s", measurement_unit.name);

	if (!measurement_unit.is_group && measurement_unit.filesystem_type) {
		xasprintf(&result.output, "%s (%s)", result.output, measurement_unit.filesystem_type);
	}

	/* Threshold comparisons */

	// ===============================
	// Free space absolute values test
	mp_subcheck freespace_bytes_sc = mp_subcheck_init();
	freespace_bytes_sc = mp_set_subcheck_default_state(freespace_bytes_sc, STATE_OK);

	if (unit != Humanized) {
		xasprintf(&freespace_bytes_sc.output, "Free space absolute: %ju%s (of %ju%s)",
				  (uintmax_t)(measurement_unit.free_bytes / unit), get_unit_string(unit),
				  (uintmax_t)(measurement_unit.total_bytes / unit), get_unit_string(unit));
	} else {
		xasprintf(&freespace_bytes_sc.output, "Free space absolute: %s (of %s)",
				  humanize_byte_value(measurement_unit.free_bytes, false),
				  humanize_byte_value((unsigned long long)measurement_unit.total_bytes, false));
	}

	mp_perfdata used_space = perfdata_init();
	used_space.label = measurement_unit.name;
	used_space.value = mp_create_pd_value(measurement_unit.free_bytes);
	used_space = mp_set_pd_max_value(used_space, mp_create_pd_value(measurement_unit.total_bytes));
	used_space = mp_set_pd_min_value(used_space, mp_create_pd_value(0));
	used_space.uom = "B";
	used_space = mp_pd_set_thresholds(used_space, measurement_unit.freespace_bytes_thresholds);
	freespace_bytes_sc = mp_set_subcheck_state(freespace_bytes_sc, mp_get_pd_status(used_space));

	// special case for absolute space thresholds here:
	// if absolute values are not set, compute the thresholds from percentage thresholds
	mp_thresholds temp_thlds = measurement_unit.freespace_bytes_thresholds;
	if (!temp_thlds.critical_is_set &&
		measurement_unit.freespace_percent_thresholds.critical_is_set) {
		mp_range tmp_range = measurement_unit.freespace_percent_thresholds.critical;

		if (!tmp_range.end_infinity) {
			tmp_range.end = mp_create_pd_value(mp_get_pd_value(tmp_range.end) / 100 *
											   measurement_unit.total_bytes);
		}

		if (!tmp_range.start_infinity) {
			tmp_range.start = mp_create_pd_value(mp_get_pd_value(tmp_range.start) / 100 *
												 measurement_unit.total_bytes);
		}
		measurement_unit.freespace_bytes_thresholds =
			mp_thresholds_set_crit(measurement_unit.freespace_bytes_thresholds, tmp_range);
		used_space = mp_pd_set_thresholds(used_space, measurement_unit.freespace_bytes_thresholds);
	}

	if (!temp_thlds.warning_is_set &&
		measurement_unit.freespace_percent_thresholds.warning_is_set) {
		mp_range tmp_range = measurement_unit.freespace_percent_thresholds.warning;
		if (!tmp_range.end_infinity) {
			tmp_range.end = mp_create_pd_value(mp_get_pd_value(tmp_range.end) / 100 *
											   measurement_unit.total_bytes);
		}
		if (!tmp_range.start_infinity) {
			tmp_range.start = mp_create_pd_value(mp_get_pd_value(tmp_range.start) / 100 *
												 measurement_unit.total_bytes);
		}
		measurement_unit.freespace_bytes_thresholds =
			mp_thresholds_set_warn(measurement_unit.freespace_bytes_thresholds, tmp_range);
		used_space = mp_pd_set_thresholds(used_space, measurement_unit.freespace_bytes_thresholds);
	}

	mp_add_perfdata_to_subcheck(&freespace_bytes_sc, used_space);
	mp_add_subcheck_to_subcheck(&result, freespace_bytes_sc);

	// ==========================
	// Free space percentage test
	mp_subcheck freespace_percent_sc = mp_subcheck_init();
	freespace_percent_sc = mp_set_subcheck_default_state(freespace_percent_sc, STATE_OK);

	double free_percentage =
		calculate_percent(measurement_unit.free_bytes, measurement_unit.total_bytes);
	xasprintf(&freespace_percent_sc.output, "Free space percentage: %g%%", free_percentage);

	// Using perfdata here just to get to the test result
	mp_perfdata free_space_percent_pd = perfdata_init();
	free_space_percent_pd.value = mp_create_pd_value(free_percentage);
	free_space_percent_pd =
		mp_pd_set_thresholds(free_space_percent_pd, measurement_unit.freespace_percent_thresholds);

	freespace_percent_sc =
		mp_set_subcheck_state(freespace_percent_sc, mp_get_pd_status(free_space_percent_pd));
	mp_add_subcheck_to_subcheck(&result, freespace_percent_sc);

	// ================
	// Free inodes test
	// Only ever useful if the number of inodes is static (e.g. ext4),
	// not when it is dynamic (e.g btrfs)
	// Assumption: if the total number of inodes == 0, we have such a case and just skip the test
	if (measurement_unit.inodes_total > 0) {
		mp_subcheck freeindodes_percent_sc = mp_subcheck_init();
		freeindodes_percent_sc = mp_set_subcheck_default_state(freeindodes_percent_sc, STATE_OK);

		double free_inode_percentage =
			calculate_percent(measurement_unit.inodes_free, measurement_unit.inodes_total);

		if (verbose > 0) {
			printf("free inode percentage computed: %g\n", free_inode_percentage);
		}

		xasprintf(&freeindodes_percent_sc.output, "Inodes free: %g%% (%ju of %ju)",
				  free_inode_percentage, measurement_unit.inodes_free,
				  measurement_unit.inodes_total);

		mp_perfdata inodes_pd = perfdata_init();
		xasprintf(&inodes_pd.label, "%s (inodes)", measurement_unit.name);
		inodes_pd = mp_set_pd_value(inodes_pd, measurement_unit.inodes_used);
		inodes_pd =
			mp_set_pd_max_value(inodes_pd, mp_create_pd_value(measurement_unit.inodes_total));
		inodes_pd = mp_set_pd_min_value(inodes_pd, mp_create_pd_value(0));

		mp_thresholds absolut_inode_thresholds = measurement_unit.freeinodes_percent_thresholds;

		if (absolut_inode_thresholds.critical_is_set) {
			absolut_inode_thresholds.critical =
				mp_range_multiply(absolut_inode_thresholds.critical,
								  mp_create_pd_value(measurement_unit.inodes_total / 100));
		}
		if (absolut_inode_thresholds.warning_is_set) {
			absolut_inode_thresholds.warning =
				mp_range_multiply(absolut_inode_thresholds.warning,
								  mp_create_pd_value(measurement_unit.inodes_total / 100));
		}

		inodes_pd = mp_pd_set_thresholds(inodes_pd, absolut_inode_thresholds);

		freeindodes_percent_sc =
			mp_set_subcheck_state(freeindodes_percent_sc, mp_get_pd_status(inodes_pd));
		if (display_inodes_perfdata) {
			mp_add_perfdata_to_subcheck(&freeindodes_percent_sc, inodes_pd);
		}
		mp_add_subcheck_to_subcheck(&result, freeindodes_percent_sc);
	}

	return result;
}
