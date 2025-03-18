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
#include "./check_disk.d/utils_disk.h"
#include "../gl/fsusage.h"
#include "../gl/mountlist.h"
#include "check_disk.d/config.h"

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
static void set_all_thresholds(struct parameter_list *path, char * /*warn_freespace_units*/, char * /*crit_freespace_units*/,
							   char * /*warn_freespace_percent*/, char * /*crit_freespace_percent*/, char * /*warn_usedspace_units*/,
							   char * /*crit_usedspace_units*/, char * /*warn_usedspace_percent*/, char * /*crit_usedspace_percent*/,
							   char * /*warn_usedinodes_percent*/, char * /*crit_usedinodes_percent*/, char * /*warn_freeinodes_percent*/,
							   char * /*crit_freeinodes_percent*/);
static void print_help(void);
void print_usage(void);
static double calculate_percent(uintmax_t /*value*/, uintmax_t /*total*/);
static bool stat_path(struct parameter_list * /*parameters*/, bool /*ignore_missing*/);
static void get_stats(struct parameter_list * /*parameters*/, struct fs_usage *fsp, bool /*ignore_missing*/,
					  bool /*freespace_ignore_reserved*/, uintmax_t /*mult*/, struct parameter_list * /*path_select_list*/,
					  struct name_list * /*seen*/);
static void get_path_stats(struct parameter_list * /*parameters*/, struct fs_usage *fsp, bool /*freespace_ignore_reserved*/,
						   uintmax_t /*mult*/, struct name_list * /*seen*/);

static int verbose = 0;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

#ifdef __CYGWIN__
	char mountdir[32];
#endif

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_disk_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_disk_config config = tmp_config.config;

	/* If a list of paths has not been selected, find entire
	   mount list and create list of paths
	 */
	if (!config.path_selected && !config.path_ignored) {
		for (struct mount_entry *me = config.mount_list; me; me = me->me_next) {
			struct parameter_list *path = NULL;
			if (!(path = np_find_parameter(config.path_select_list, me->me_mountdir))) {
				path = np_add_parameter(&config.path_select_list, me->me_mountdir);
			}
			path->best_match = me;
			path->group = config.group;
			set_all_thresholds(path, config.warn_freespace_units, config.crit_freespace_units, config.warn_freespace_percent,
							   config.crit_freespace_percent, config.warn_usedspace_units, config.crit_usedspace_units,
							   config.warn_usedspace_percent, config.crit_usedspace_percent, config.warn_usedinodes_percent,
							   config.crit_usedinodes_percent, config.warn_freeinodes_percent, config.crit_freeinodes_percent);
		}
	}

	if (!config.path_ignored) {
		np_set_best_match(config.path_select_list, config.mount_list, config.exact_match);
	}

	/* Error if no match found for specified paths */
	struct parameter_list *temp_list = config.path_select_list;

	char *ignored = strdup("");
	while (config.path_select_list) {
		if (!config.path_select_list->best_match && config.ignore_missing) {
			/* If the first element will be deleted, the temp_list must be updated with the new start address as well */
			if (config.path_select_list == temp_list) {
				temp_list = config.path_select_list->name_next;
			}
			/* Add path argument to list of ignored paths to inform about missing paths being ignored and not alerted */
			xasprintf(&ignored, "%s %s;", ignored, config.path_select_list->name);
			/* Delete the path from the list so that it is not stat-checked later in the code. */
			config.path_select_list = np_del_parameter(config.path_select_list, config.path_select_list->name_prev);
		} else if (!config.path_select_list->best_match) {
			/* Without --ignore-missing option, exit with Critical state. */
			die(STATE_CRITICAL, _("DISK %s: %s not found\n"), _("CRITICAL"), config.path_select_list->name);
		} else {
			/* Continue jumping through the list */
			config.path_select_list = config.path_select_list->name_next;
		}
	}

	config.path_select_list = temp_list;

	mp_state_enum result = STATE_UNKNOWN;
	if (!config.path_select_list && config.ignore_missing) {
		result = STATE_OK;
		if (verbose >= 2) {
			printf("None of the provided paths were found\n");
		}
	}

	mp_state_enum filesystem_result = STATE_UNKNOWN;
	char *perf = strdup("");
	char *perf_ilabel = strdup("");
	char *output = strdup("");
	struct parameter_list *path = NULL;
	/* Process for every path in list */
	for (path = config.path_select_list; path; path = path->name_next) {
		if (verbose >= 3 && path->freespace_percent->warning != NULL && path->freespace_percent->critical != NULL) {
			printf("Thresholds(pct) for %s warn: %f crit %f\n", path->name, path->freespace_percent->warning->end,
				   path->freespace_percent->critical->end);
		}

		if (verbose >= 3 && path->group != NULL) {
			printf("Group of %s: %s\n", path->name, path->group);
		}

		// reset filesystem result
		filesystem_result = STATE_UNKNOWN;

		struct mount_entry *mount_entry = path->best_match;

		if (!mount_entry) {
			continue;
		}

#ifdef __CYGWIN__
		if (strncmp(path->name, "/cygdrive/", 10) != 0 || strlen(path->name) > 11) {
			continue;
		}
		snprintf(mountdir, sizeof(mountdir), "%s:\\", me->me_mountdir + 10);
		if (GetDriveType(mountdir) != DRIVE_FIXED) {
			me->me_remote = 1;
		}
#endif
		/* Filters */

		/* Remove filesystems already seen */
		if (np_seen_name(config.seen, mount_entry->me_mountdir)) {
			continue;
		}
		np_add_name(&config.seen, mount_entry->me_mountdir);

		if (path->group == NULL) {
			/* Skip remote filesystems if we're not interested in them */
			if (mount_entry->me_remote && config.show_local_fs) {
				if (config.stat_remote_fs) {
					if (!stat_path(path, config.ignore_missing) && config.ignore_missing) {
						result = STATE_OK;
						xasprintf(&ignored, "%s %s;", ignored, path->name);
					}
				}
				continue;
				/* Skip pseudo fs's if we haven't asked for all fs's */
			}
			if (config.fs_exclude_list && np_find_regmatch(config.fs_exclude_list, mount_entry->me_type)) {
				continue;
				/* Skip excluded fs's */
			}
			if (config.device_path_exclude_list && (np_find_name(config.device_path_exclude_list, mount_entry->me_devname) ||
													np_find_name(config.device_path_exclude_list, mount_entry->me_mountdir))) {
				continue;
				/* Skip not included fstypes */
			}
			if (config.fs_include_list && !np_find_regmatch(config.fs_include_list, mount_entry->me_type)) {
				continue;
			}
		}

		if (!stat_path(path, config.ignore_missing)) {
			if (config.ignore_missing) {
				result = STATE_OK;
				xasprintf(&ignored, "%s %s;", ignored, path->name);
			}
			continue;
		}

		struct fs_usage fsp = {0};
		get_fs_usage(mount_entry->me_mountdir, mount_entry->me_devname, &fsp);

		if (fsp.fsu_blocks && strcmp("none", mount_entry->me_mountdir)) {
			get_stats(path, &fsp, config.ignore_missing, config.freespace_ignore_reserved, config.mult, config.path_select_list,
					  config.seen);

			if (verbose >= 3) {
				printf("For %s, used_pct=%f free_pct=%f used_units=%lu free_units=%lu total_units=%lu used_inodes_pct=%f "
					   "free_inodes_pct=%f fsp.fsu_blocksize=%lu mult=%lu\n",
					   mount_entry->me_mountdir, path->dused_pct, path->dfree_pct, path->dused_units, path->dfree_units, path->dtotal_units,
					   path->dused_inodes_percent, path->dfree_inodes_percent, fsp.fsu_blocksize, config.mult);
			}

			/* Threshold comparisons */

			mp_state_enum temp_result = get_status(path->dfree_units, path->freespace_units);
			if (verbose >= 3) {
				printf("Freespace_units result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			temp_result = get_status(path->dfree_pct, path->freespace_percent);
			if (verbose >= 3) {
				printf("Freespace%% result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			temp_result = get_status(path->dused_units, path->usedspace_units);
			if (verbose >= 3) {
				printf("Usedspace_units result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			temp_result = get_status(path->dused_pct, path->usedspace_percent);
			if (verbose >= 3) {
				printf("Usedspace_percent result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			temp_result = get_status(path->dused_inodes_percent, path->usedinodes_percent);
			if (verbose >= 3) {
				printf("Usedinodes_percent result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			temp_result = get_status(path->dfree_inodes_percent, path->freeinodes_percent);
			if (verbose >= 3) {
				printf("Freeinodes_percent result=%d\n", temp_result);
			}
			filesystem_result = max_state(filesystem_result, temp_result);

			result = max_state(result, filesystem_result);

			/* What a mess of units. The output shows free space, the perf data shows used space. Yikes!
			   Hack here. Trying to get warn/crit levels from freespace_(units|percent) for perf
			   data. Assumption that start=0. Roll on new syntax...
			*/

			/* *_high_tide must be reinitialized at each run */
			uint64_t warning_high_tide = UINT64_MAX;

			if (path->freespace_units->warning != NULL) {
				warning_high_tide = (path->dtotal_units - path->freespace_units->warning->end) * config.mult;
			}
			if (path->freespace_percent->warning != NULL) {
				warning_high_tide = min(warning_high_tide, (uint64_t)((1.0 - path->freespace_percent->warning->end / 100) *
																	  (path->dtotal_units * config.mult)));
			}

			uint64_t critical_high_tide = UINT64_MAX;

			if (path->freespace_units->critical != NULL) {
				critical_high_tide = (path->dtotal_units - path->freespace_units->critical->end) * config.mult;
			}
			if (path->freespace_percent->critical != NULL) {
				critical_high_tide = min(critical_high_tide, (uint64_t)((1.0 - path->freespace_percent->critical->end / 100) *
																		(path->dtotal_units * config.mult)));
			}

			/* Nb: *_high_tide are unset when == UINT64_MAX */
			xasprintf(&perf, "%s %s", perf,
					  perfdata_uint64((!strcmp(mount_entry->me_mountdir, "none") || config.display_mntp) ? mount_entry->me_devname
																										 : mount_entry->me_mountdir,
									  path->dused_units * config.mult, "B", (warning_high_tide != UINT64_MAX), warning_high_tide,
									  (critical_high_tide != UINT64_MAX), critical_high_tide, true, 0, true,
									  path->dtotal_units * config.mult));

			if (config.display_inodes_perfdata) {
				/* *_high_tide must be reinitialized at each run */
				warning_high_tide = UINT64_MAX;
				critical_high_tide = UINT64_MAX;

				if (path->freeinodes_percent->warning != NULL) {
					warning_high_tide = (uint64_t)fabs(
						min((double)warning_high_tide, (double)(1.0 - path->freeinodes_percent->warning->end / 100) * path->inodes_total));
				}
				if (path->freeinodes_percent->critical != NULL) {
					critical_high_tide = (uint64_t)fabs(min(
						(double)critical_high_tide, (double)(1.0 - path->freeinodes_percent->critical->end / 100) * path->inodes_total));
				}

				xasprintf(&perf_ilabel, "%s (inodes)",
						  (!strcmp(mount_entry->me_mountdir, "none") || config.display_mntp) ? mount_entry->me_devname
																							 : mount_entry->me_mountdir);
				/* Nb: *_high_tide are unset when == UINT64_MAX */
				xasprintf(&perf, "%s %s", perf,
						  perfdata_uint64(perf_ilabel, path->inodes_used, "", (warning_high_tide != UINT64_MAX), warning_high_tide,
										  (critical_high_tide != UINT64_MAX), critical_high_tide, true, 0, true, path->inodes_total));
			}

			if (filesystem_result == STATE_OK && config.erronly && !verbose) {
				continue;
			}

			char *flag_header = NULL;
			if (filesystem_result && verbose >= 1) {
				xasprintf(&flag_header, " %s [", state_text(filesystem_result));
			} else {
				xasprintf(&flag_header, "");
			}
			xasprintf(&output, "%s%s %s %llu%s (%.1f%%", output, flag_header,
					  (!strcmp(mount_entry->me_mountdir, "none") || config.display_mntp) ? mount_entry->me_devname
																						 : mount_entry->me_mountdir,
					  path->dfree_units, config.units, path->dfree_pct);
			if (path->dused_inodes_percent < 0) {
				xasprintf(&output, "%s inode=-)%s;", output, (filesystem_result ? "]" : ""));
			} else {
				xasprintf(&output, "%s inode=%.0f%%)%s;", output, path->dfree_inodes_percent,
						  ((filesystem_result && verbose >= 1) ? "]" : ""));
			}
			free(flag_header);
		}
	}

	char *preamble = " - free space:";
	if (strcmp(output, "") == 0 && !config.erronly) {
		preamble = "";
		xasprintf(&output, " - No disks were found for provided parameters");
	}

	char *ignored_preamble = " - ignored paths:";
	printf("DISK %s%s%s%s%s|%s\n", state_text(result), (config.erronly && (result == STATE_OK)) ? "" : preamble, output,
		   (strcmp(ignored, "") == 0) ? "" : ignored_preamble, ignored, perf);
	return result;
}

double calculate_percent(uintmax_t value, uintmax_t total) {
	double pct = -1;
	if (value <= DBL_MAX && total != 0) {
		pct = (double)value / total * 100.0;
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

	static struct option longopts[] = {{"timeout", required_argument, 0, 't'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"iwarning", required_argument, 0, 'W'},
									   /* Dang, -C is taken. We might want to reshuffle this. */
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
									   {0, 0, 0, 0}};

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
	}

	int cflags = REG_NOSUB | REG_EXTENDED;
	int default_cflags = cflags;
	result.config.mount_list = read_file_system_list(false);

	np_add_regex(&result.config.fs_exclude_list, "iso9660", REG_EXTENDED);

	while (true) {
		int option = 0;
		int option_index = getopt_long(argc, argv, "+?VqhvefCt:c:w:K:W:u:p:x:X:N:mklLPg:R:r:i:I:MEAn", longopts, &option);

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
					result.config.warn_freespace_percent = optarg;
				} else {
					xasprintf(&result.config.warn_freespace_percent, "@%s", optarg);
				}
			} else {
				if (*optarg == '@') {
					result.config.warn_freespace_units = optarg;
				} else {
					xasprintf(&result.config.warn_freespace_units, "@%s", optarg);
				}
			}
			break;

		/* Awful mistake where the range values do not make sense. Normally,
		   you alert if the value is within the range, but since we are using
		   freespace, we have to alert if outside the range. Thus we artificially
		   force @ at the beginning of the range, so that it is backwards compatible
		*/
		case 'c': /* critical threshold */
			if (!is_percentage_expression(optarg) && !is_numeric(optarg)) {
				die(STATE_UNKNOWN, "Argument for --critical invalid or missing: %s\n", optarg);
			}

			if (strstr(optarg, "%")) {
				if (*optarg == '@') {
					result.config.crit_freespace_percent = optarg;
				} else {
					xasprintf(&result.config.crit_freespace_percent, "@%s", optarg);
				}
			} else {
				if (*optarg == '@') {
					result.config.crit_freespace_units = optarg;
				} else {
					xasprintf(&result.config.crit_freespace_units, "@%s", optarg);
				}
			}
			break;

		case 'W': /* warning inode threshold */
			if (*optarg == '@') {
				result.config.warn_freeinodes_percent = optarg;
			} else {
				xasprintf(&result.config.warn_freeinodes_percent, "@%s", optarg);
			}
			break;
		case 'K': /* critical inode threshold */
			if (*optarg == '@') {
				result.config.crit_freeinodes_percent = optarg;
			} else {
				xasprintf(&result.config.crit_freeinodes_percent, "@%s", optarg);
			}
			break;
		case 'u':
			free(result.config.units);
			if (!strcasecmp(optarg, "bytes")) {
				result.config.mult = (uintmax_t)1;
				result.config.units = strdup("B");
			} else if (!strcmp(optarg, "KiB")) {
				result.config.mult = (uintmax_t)1024;
				result.config.units = strdup("KiB");
			} else if (!strcmp(optarg, "kB")) {
				result.config.mult = (uintmax_t)1000;
				result.config.units = strdup("kB");
			} else if (!strcmp(optarg, "MiB")) {
				result.config.mult = (uintmax_t)1024 * 1024;
				result.config.units = strdup("MiB");
			} else if (!strcmp(optarg, "MB")) {
				result.config.mult = (uintmax_t)1000 * 1000;
				result.config.units = strdup("MB");
			} else if (!strcmp(optarg, "GiB")) {
				result.config.mult = (uintmax_t)1024 * 1024 * 1024;
				result.config.units = strdup("GiB");
			} else if (!strcmp(optarg, "GB")) {
				result.config.mult = (uintmax_t)1000 * 1000 * 1000;
				result.config.units = strdup("GB");
			} else if (!strcmp(optarg, "TiB")) {
				result.config.mult = (uintmax_t)1024 * 1024 * 1024 * 1024;
				result.config.units = strdup("TiB");
			} else if (!strcmp(optarg, "TB")) {
				result.config.mult = (uintmax_t)1000 * 1000 * 1000 * 1000;
				result.config.units = strdup("TB");
			} else if (!strcmp(optarg, "PiB")) {
				result.config.mult = (uintmax_t)1024 * 1024 * 1024 * 1024 * 1024;
				result.config.units = strdup("PiB");
			} else if (!strcmp(optarg, "PB")) {
				result.config.mult = (uintmax_t)1000 * 1000 * 1000 * 1000 * 1000;
				result.config.units = strdup("PB");
			} else {
				die(STATE_UNKNOWN, _("unit type %s not known\n"), optarg);
			}
			if (result.config.units == NULL) {
				die(STATE_UNKNOWN, _("failed allocating storage for '%s'\n"), "units");
			}
			break;
		case 'k': /* display mountpoint */
			result.config.mult = 1024;
			free(result.config.units);
			result.config.units = strdup("kiB");
			break;
		case 'm': /* display mountpoint */
			result.config.mult = 1024 * 1024;
			free(result.config.units);
			result.config.units = strdup("MiB");
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
			if (!(result.config.warn_freespace_units || result.config.crit_freespace_units || result.config.warn_freespace_percent ||
				  result.config.crit_freespace_percent || result.config.warn_usedspace_units || result.config.crit_usedspace_units ||
				  result.config.warn_usedspace_percent || result.config.crit_usedspace_percent || result.config.warn_usedinodes_percent ||
				  result.config.crit_usedinodes_percent || result.config.warn_freeinodes_percent ||
				  result.config.crit_freeinodes_percent)) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set a threshold value before using -p\n"));
			}

			/* add parameter if not found. overwrite thresholds if path has already been added  */
			struct parameter_list *se;
			if (!(se = np_find_parameter(result.config.path_select_list, optarg))) {
				se = np_add_parameter(&result.config.path_select_list, optarg);

				struct stat stat_buf = {};
				if (stat(optarg, &stat_buf) && result.config.ignore_missing) {
					result.config.path_ignored = true;
					break;
				}
			}
			se->group = result.config.group;
			set_all_thresholds(
				se, result.config.warn_freespace_units, result.config.crit_freespace_units, result.config.warn_freespace_percent,
				result.config.crit_freespace_percent, result.config.warn_usedspace_units, result.config.crit_usedspace_units,
				result.config.warn_usedspace_percent, result.config.crit_usedspace_percent, result.config.warn_usedinodes_percent,
				result.config.crit_usedinodes_percent, result.config.warn_freeinodes_percent, result.config.crit_freeinodes_percent);

			/* With autofs, it is required to stat() the path before re-populating the mount_list */
			if (!stat_path(se, result.config.ignore_missing)) {
				break;
			}
			/* NB: We can't free the old mount_list "just like that": both list pointers and struct
			 * pointers are copied around. One of the reason it wasn't done yet is that other parts
			 * of check_disk need the same kind of cleanup so it'd better be done as a whole */
			result.config.mount_list = read_file_system_list(false);
			np_set_best_match(se, result.config.mount_list, result.config.exact_match);

			result.config.path_selected = true;
		} break;
		case 'x': /* exclude path or partition */
			np_add_name(&result.config.device_path_exclude_list, optarg);
			break;
		case 'X': /* exclude file system type */ {
			int err = np_add_regex(&result.config.fs_exclude_list, optarg, REG_EXTENDED);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &result.config.fs_exclude_list->regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
			}
			break;
		case 'N': /* include file system type */
			err = np_add_regex(&result.config.fs_include_list, optarg, REG_EXTENDED);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &result.config.fs_exclude_list->regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
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
			if (result.config.path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set -E before selecting paths\n"));
			}
			result.config.exact_match = true;
			break;
		case 'f':
			result.config.freespace_ignore_reserved = true;
			break;
		case 'g':
			if (result.config.path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"), _("Must set group value before selecting paths\n"));
			}
			result.config.group = optarg;
			break;
		case 'I':
			cflags |= REG_ICASE;
			// Intentional fallthrough
		case 'i': {
			if (!result.config.path_selected) {
				die(STATE_UNKNOWN, "DISK %s: %s\n", _("UNKNOWN"),
					_("Paths need to be selected before using -i/-I. Use -A to select all paths explicitly"));
			}
			regex_t regex;
			int err = regcomp(&regex, optarg, cflags);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
			}

			struct parameter_list *temp_list = result.config.path_select_list;
			struct parameter_list *previous = NULL;
			while (temp_list) {
				if (temp_list->best_match) {
					if (np_regex_match_mount_entry(temp_list->best_match, &regex)) {

						if (verbose >= 3) {
							printf("ignoring %s matching regex\n", temp_list->name);
						}

						temp_list = np_del_parameter(temp_list, previous);
						/* pointer to first element needs to be updated if first item gets deleted */
						if (previous == NULL) {
							result.config.path_select_list = temp_list;
						}
					} else {
						previous = temp_list;
						temp_list = temp_list->name_next;
					}
				} else {
					previous = temp_list;
					temp_list = temp_list->name_next;
				}
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
			if (!(result.config.warn_freespace_units || result.config.crit_freespace_units || result.config.warn_freespace_percent ||
				  result.config.crit_freespace_percent || result.config.warn_usedspace_units || result.config.crit_usedspace_units ||
				  result.config.warn_usedspace_percent || result.config.crit_usedspace_percent || result.config.warn_usedinodes_percent ||
				  result.config.crit_usedinodes_percent || result.config.warn_freeinodes_percent ||
				  result.config.crit_freeinodes_percent)) {
				die(STATE_UNKNOWN, "DISK %s: %s", _("UNKNOWN"),
					_("Must set a threshold value before using -r/-R/-A (--ereg-path/--eregi-path/--all)\n"));
			}

			regex_t regex;
			int err = regcomp(&regex, optarg, cflags);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &regex, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
			}

			bool found = false;
			for (struct mount_entry *me = result.config.mount_list; me; me = me->me_next) {
				if (np_regex_match_mount_entry(me, &regex)) {
					found = true;
					if (verbose >= 3) {
						printf("%s %s matching expression %s\n", me->me_devname, me->me_mountdir, optarg);
					}

					/* add parameter if not found. overwrite thresholds if path has already been added  */
					struct parameter_list *se = NULL;
					if (!(se = np_find_parameter(result.config.path_select_list, me->me_mountdir))) {
						se = np_add_parameter(&result.config.path_select_list, me->me_mountdir);
					}
					se->group = result.config.group;
					set_all_thresholds(se, result.config.warn_freespace_units, result.config.crit_freespace_units,
									   result.config.warn_freespace_percent, result.config.crit_freespace_percent,
									   result.config.warn_usedspace_units, result.config.crit_usedspace_units,
									   result.config.warn_usedspace_percent, result.config.crit_usedspace_percent,
									   result.config.warn_usedinodes_percent, result.config.crit_usedinodes_percent,
									   result.config.warn_freeinodes_percent, result.config.crit_freeinodes_percent);
				}
			}

			if (!found && result.config.ignore_missing) {
				result.config.path_ignored = true;
				result.config.path_selected = true;
				break;
			}
			if (!found) {
				die(STATE_UNKNOWN, "DISK %s: %s - %s\n", _("UNKNOWN"), _("Regular expression did not match any path or disk"), optarg);
			}

			result.config.path_selected = true;
			np_set_best_match(result.config.path_select_list, result.config.mount_list, result.config.exact_match);
			cflags = default_cflags;

		} break;
		case 'M': /* display mountpoint */
			result.config.display_mntp = true;
			break;
		case 'C': {
			/* add all mount entries to path_select list if no partitions have been explicitly defined using -p */
			if (!result.config.path_selected) {
				struct parameter_list *path;
				for (struct mount_entry *me = result.config.mount_list; me; me = me->me_next) {
					if (!(path = np_find_parameter(result.config.path_select_list, me->me_mountdir))) {
						path = np_add_parameter(&result.config.path_select_list, me->me_mountdir);
					}
					path->best_match = me;
					path->group = result.config.group;
					set_all_thresholds(path, result.config.warn_freespace_units, result.config.crit_freespace_units,
									   result.config.warn_freespace_percent, result.config.crit_freespace_percent,
									   result.config.warn_usedspace_units, result.config.crit_usedspace_units,
									   result.config.warn_usedspace_percent, result.config.crit_usedspace_percent,
									   result.config.warn_usedinodes_percent, result.config.crit_usedinodes_percent,
									   result.config.warn_freeinodes_percent, result.config.crit_freeinodes_percent);
				}
			}
			result.config.warn_freespace_units = NULL;
			result.config.crit_freespace_units = NULL;
			result.config.warn_usedspace_units = NULL;
			result.config.crit_usedspace_units = NULL;
			result.config.warn_freespace_percent = NULL;
			result.config.crit_freespace_percent = NULL;
			result.config.warn_usedspace_percent = NULL;
			result.config.crit_usedspace_percent = NULL;
			result.config.warn_usedinodes_percent = NULL;
			result.config.crit_usedinodes_percent = NULL;
			result.config.warn_freeinodes_percent = NULL;
			result.config.crit_freeinodes_percent = NULL;

			result.config.path_selected = false;
			result.config.group = NULL;
		} break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case '?': /* help */
			usage(_("Unknown argument"));
		}
	}

	/* Support for "check_disk warn crit [fs]" with thresholds at used% level */
	int index = optind;

	if (result.config.warn_usedspace_percent == NULL && argc > index && is_intnonneg(argv[index])) {
		if (verbose > 0) {
			printf("Got an positional warn threshold: %s\n", argv[index]);
		}
		result.config.warn_usedspace_percent = argv[index++];
	}

	if (result.config.crit_usedspace_percent == NULL && argc > index && is_intnonneg(argv[index])) {
		if (verbose > 0) {
			printf("Got an positional crit threshold: %s\n", argv[index]);
		}
		result.config.crit_usedspace_percent = argv[index++];
	}

	if (argc > index) {
		if (verbose > 0) {
			printf("Got an positional filesystem: %s\n", argv[index]);
		}
		struct parameter_list *se = np_add_parameter(&result.config.path_select_list, strdup(argv[index++]));
		result.config.path_selected = true;
		set_all_thresholds(se, result.config.warn_freespace_units, result.config.crit_freespace_units, result.config.warn_freespace_percent,
						   result.config.crit_freespace_percent, result.config.warn_usedspace_units, result.config.crit_usedspace_units,
						   result.config.warn_usedspace_percent, result.config.crit_usedspace_percent,
						   result.config.warn_usedinodes_percent, result.config.crit_usedinodes_percent,
						   result.config.warn_freeinodes_percent, result.config.crit_freeinodes_percent);
	}

	if (result.config.units == NULL) {
		result.config.units = strdup("MiB");
		result.config.mult = (uintmax_t)1024 * 1024;
	}

	return result;
}

void set_all_thresholds(struct parameter_list *path, char *warn_freespace_units, char *crit_freespace_units, char *warn_freespace_percent,
						char *crit_freespace_percent, char *warn_usedspace_units, char *crit_usedspace_units, char *warn_usedspace_percent,
						char *crit_usedspace_percent, char *warn_usedinodes_percent, char *crit_usedinodes_percent,
						char *warn_freeinodes_percent, char *crit_freeinodes_percent) {
	free(path->freespace_units);
	set_thresholds(&path->freespace_units, warn_freespace_units, crit_freespace_units);

	free(path->freespace_percent);
	set_thresholds(&path->freespace_percent, warn_freespace_percent, crit_freespace_percent);

	free(path->usedspace_units);
	set_thresholds(&path->usedspace_units, warn_usedspace_units, crit_usedspace_units);

	free(path->usedspace_percent);
	set_thresholds(&path->usedspace_percent, warn_usedspace_percent, crit_usedspace_percent);

	free(path->usedinodes_percent);
	set_thresholds(&path->usedinodes_percent, warn_usedinodes_percent, crit_usedinodes_percent);

	free(path->freeinodes_percent);
	set_thresholds(&path->freeinodes_percent, warn_freeinodes_percent, crit_freeinodes_percent);
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin checks the amount of used disk space on a mounted file system"));
	printf("%s\n", _("and generates an alert if free space is less than one of the threshold values"));

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
	printf("    %s\n", _("Mount point or block device as emitted by the mount(8) command (may be repeated)"));
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
	printf("    %s\n", _("Group paths. Thresholds apply to (free-)space of all partitions together"));
	printf(" %s\n", "-k, --kilobytes");
	printf("    %s\n", _("Same as '--units kB'"));
	printf(" %s\n", "-l, --local");
	printf("    %s\n", _("Only check local filesystems"));
	printf(" %s\n", "-L, --stat-remote-fs");
	printf("    %s\n", _("Only check local filesystems against thresholds. Yet call stat on remote filesystems"));
	printf("    %s\n", _("to test if they are accessible (e.g. to detect Stale NFS Handles)"));
	printf(" %s\n", "-M, --mountpoint");
	printf("    %s\n", _("Display the (block) device instead of the mount point"));
	printf(" %s\n", "-m, --megabytes");
	printf("    %s\n", _("Same as '--units MB'"));
	printf(" %s\n", "-A, --all");
	printf("    %s\n", _("Explicitly select all paths. This is equivalent to -R '.*'"));
	printf(" %s\n", "-R, --eregi-path=PATH, --eregi-partition=PARTITION");
	printf("    %s\n", _("Case insensitive regular expression for path/partition (may be repeated)"));
	printf(" %s\n", "-r, --ereg-path=PATH, --ereg-partition=PARTITION");
	printf("    %s\n", _("Regular expression for path or partition (may be repeated)"));
	printf(" %s\n", "-I, --ignore-eregi-path=PATH, --ignore-eregi-partition=PARTITION");
	printf("    %s\n", _("Regular expression to ignore selected path/partition (case insensitive) (may be repeated)"));
	printf(" %s\n", "-i, --ignore-ereg-path=PATH, --ignore-ereg-partition=PARTITION");
	printf("    %s\n", _("Regular expression to ignore selected path or partition (may be repeated)"));
	printf(" %s\n", "-n, --ignore-missing");
	printf("    %s\n", _("Return OK if no filesystem matches, filesystem does not exist or is inaccessible."));
	printf("    %s\n", _("(Provide this option before -p / -r / --ereg-path if used)"));
	printf(UT_PLUG_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf(" %s\n", "-u, --units=STRING");
	printf("    %s\n", _("Choose bytes, kB, MB, GB, TB (default: MB)"));
	printf(UT_VERBOSE);
	printf(" %s\n", "-X, --exclude-type=TYPE_REGEX");
	printf("    %s\n", _("Ignore all filesystems of types matching given regex(7) (may be repeated)"));
	printf(" %s\n", "-N, --include-type=TYPE_REGEX");
	printf("    %s\n", _("Check only filesystems where the type matches this given regex(7) (may be repeated)"));

	printf("\n");
	printf("%s\n", _("General usage hints:"));
	printf(" %s\n", _("- Arguments are positional! \"-w 5 -c 1 -p /foo -w6 -c2 -p /bar\" is not the same as"));
	printf("   %s\n", _("\"-w 5 -c 1 -p /bar w6 -c2 -p /foo\"."));
	printf(" %s\n", _("- The syntax is broadly: \"{thresholds a} {paths a} -C {thresholds b} {thresholds b} ...\""));

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_disk -w 10% -c 5% -p /tmp -p /var -C -w 100000 -c 50000 -p /");
	printf("    %s\n\n", _("Checks /tmp and /var at 10% and 5%, and / at 100MB and 50MB"));
	printf(" %s\n", "check_disk -w 100 -c 50 -C -w 1000 -c 500 -g sidDATA -r '^/oracle/SID/data.*$'");
	printf("    %s\n", _("Checks all filesystems not matching -r at 100M and 50M. The fs matching the -r regex"));
	printf("    %s\n\n", _("are grouped which means the freespace thresholds are applied to all disks together"));
	printf(" %s\n", "check_disk -w 100 -c 50 -C -w 1000 -c 500 -p /foo -C -w 5% -c 3% -p /bar");
	printf("    %s\n", _("Checks /foo for 1000M/500M and /bar for 5/3%. All remaining volumes use 100M/50M"));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s {-w absolute_limit |-w  percentage_limit%% | -W inode_percentage_limit } {-c absolute_limit|-c percentage_limit%% | -K "
		   "inode_percentage_limit } {-p path | -x device}\n",
		   progname);
	printf("[-C] [-E] [-e] [-f] [-g group ] [-k] [-l] [-M] [-m] [-R path ] [-r path ]\n");
	printf("[-t timeout] [-u unit] [-v] [-X type_regex] [-N type]\n");
}

bool stat_path(struct parameter_list *parameters, bool ignore_missing) {
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
		die(STATE_CRITICAL, _("%s %s: %s\n"), parameters->name, _("is not accessible"), strerror(errno));
	}

	return true;
}

void get_stats(struct parameter_list *parameters, struct fs_usage *fsp, bool ignore_missing, bool freespace_ignore_reserved, uintmax_t mult,
			   struct parameter_list *path_select_list, struct name_list *seen) {
	struct fs_usage tmpfsp;
	bool first = true;

	if (parameters->group == NULL) {
		get_path_stats(parameters, fsp, freespace_ignore_reserved, mult, seen);
	} else {
		/* find all group members */
		for (struct parameter_list *p_list = path_select_list; p_list; p_list = p_list->name_next) {

#ifdef __CYGWIN__
			if (strncmp(p_list->name, "/cygdrive/", 10) != 0) {
				continue;
			}
#endif

			if (p_list->group && !(strcmp(p_list->group, parameters->group))) {
				if (!stat_path(p_list, ignore_missing)) {
					continue;
				}
				get_fs_usage(p_list->best_match->me_mountdir, p_list->best_match->me_devname, &tmpfsp);
				get_path_stats(p_list, &tmpfsp, freespace_ignore_reserved, mult, seen);
				if (verbose >= 3) {
					printf("Group %s: adding %lu blocks sized %lu, (%s) used_units=%lu free_units=%lu total_units=%lu mult=%lu\n",
						   p_list->group, tmpfsp.fsu_blocks, tmpfsp.fsu_blocksize, p_list->best_match->me_mountdir, p_list->dused_units,
						   p_list->dfree_units, p_list->dtotal_units, mult);
				}

				/* prevent counting the first FS of a group twice since its parameter_list entry
				 * is used to carry the information of all file systems of the entire group */
				if (!first) {
					parameters->total += p_list->total;
					parameters->available += p_list->available;
					parameters->available_to_root += p_list->available_to_root;
					parameters->used += p_list->used;

					parameters->dused_units += p_list->dused_units;
					parameters->dfree_units += p_list->dfree_units;
					parameters->dtotal_units += p_list->dtotal_units;
					parameters->inodes_total += p_list->inodes_total;
					parameters->inodes_free += p_list->inodes_free;
					parameters->inodes_free_to_root += p_list->inodes_free_to_root;
					parameters->inodes_used += p_list->inodes_used;
				}
				first = false;
			}
			if (verbose >= 3) {
				printf("Group %s now has: used_units=%lu free_units=%lu total_units=%lu fsu_blocksize=%lu mult=%lu\n", parameters->group,
					   parameters->dused_units, parameters->dfree_units, parameters->dtotal_units, tmpfsp.fsu_blocksize, mult);
			}
		}
		/* modify devname and mountdir for output */
		parameters->best_match->me_mountdir = parameters->best_match->me_devname = parameters->group;
	}
	/* finally calculate percentages for either plain FS or summed up group */
	parameters->dused_pct =
		calculate_percent(parameters->used, parameters->used + parameters->available); /* used + available can never be > uintmax */
	parameters->dfree_pct = 100.0 - parameters->dused_pct;
	parameters->dused_inodes_percent = calculate_percent(parameters->inodes_total - parameters->inodes_free, parameters->inodes_total);
	parameters->dfree_inodes_percent = 100 - parameters->dused_inodes_percent;
}

void get_path_stats(struct parameter_list *parameters, struct fs_usage *fsp, bool freespace_ignore_reserved, uintmax_t mult,
					struct name_list *seen) {
	parameters->available = fsp->fsu_bavail;
	parameters->available_to_root = fsp->fsu_bfree;
	parameters->used = fsp->fsu_blocks - fsp->fsu_bfree;
	if (freespace_ignore_reserved) {
		/* option activated : we subtract the root-reserved space from the total */
		parameters->total = fsp->fsu_blocks - parameters->available_to_root + parameters->available;
	} else {
		/* default behaviour : take all the blocks into account */
		parameters->total = fsp->fsu_blocks;
	}

	parameters->dused_units = parameters->used * fsp->fsu_blocksize / mult;
	parameters->dfree_units = parameters->available * fsp->fsu_blocksize / mult;
	parameters->dtotal_units = parameters->total * fsp->fsu_blocksize / mult;
	/* Free file nodes. Not sure the workaround is required, but in case...*/
	parameters->inodes_free = fsp->fsu_ffree;
	parameters->inodes_free_to_root = fsp->fsu_ffree; /* Free file nodes for root. */
	parameters->inodes_used = fsp->fsu_files - fsp->fsu_ffree;
	if (freespace_ignore_reserved) {
		/* option activated : we subtract the root-reserved inodes from the total */
		/* not all OS report fsp->fsu_favail, only the ones with statvfs syscall */
		/* for others, fsp->fsu_ffree == fsp->fsu_favail */
		parameters->inodes_total = fsp->fsu_files - parameters->inodes_free_to_root + parameters->inodes_free;
	} else {
		/* default behaviour : take all the inodes into account */
		parameters->inodes_total = fsp->fsu_files;
	}
	np_add_name(&seen, parameters->best_match->me_mountdir);
}
