/******************************************************************************
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
*****************************************************************************/

const char *progname = "check_disk";
const char *revision = "$Revision$";
const char *copyright = "1999-2003";
const char *authors = "Nagios Plugin Development Team";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

const char *summary = "\
This plugin checks the amount of used disk space on a mounted file system\n\
and generates an alert if free space is less than one of the threshold values.";

const char *option_summary = "\
-w limit -c limit [-p path | -x device] [-t timeout] [-m] [-e]\n\
        [-v] [-q]";

const char *options = "\
 -w, --warning=INTEGER\n\
   Exit with WARNING status if less than INTEGER kilobytes of disk are free\n\
 -w, --warning=PERCENT%%\n\
   Exit with WARNING status if less than PERCENT of disk space is free\n\
 -c, --critical=INTEGER\n\
   Exit with CRITICAL status if less than INTEGER kilobytes of disk are free\n\
 -c, --critical=PERCENT%%\n\
   Exit with CRITCAL status if less than PERCENT of disk space is free\n\
 -p, --path=PATH, --partition=PARTTION\n\
    Path or partition (checks all mounted partitions if unspecified)\n\
 -m, --mountpoint\n\
    Display the mountpoint instead of the partition\n\
 -x, --exclude_device=PATH\n\
    Ignore device (only works if -p unspecified)\n\
 -e, --errors-only\n\
    Display only devices/mountpoints with errors\n\
 -v, --verbose\n\
    Show details for command-line debugging (do not use with nagios server)\n\
 -h, --help\n\
    Print detailed help screen\n\
 -V, --version\n\
    Print version information\n";

#include "common.h"
#if HAVE_INTTYPES_H
# include <inttypes.h>
#endif
#include <assert.h>
#include "popen.h"
#include "utils.h"
#include <stdarg.h>
#include "../lib/fsusage.h"

/* If nonzero, show inode information. */
static int inode_format;

/* If nonzero, show even filesystems with zero size or
   uninteresting types. */
static int show_all_fs;

/* If nonzero, show only local filesystems.  */
static int show_local_fs;

/* If nonzero, output data for each filesystem corresponding to a
   command line argument -- even if it's a dummy (automounter) entry.  */
static int show_listed_fs;

/* If positive, the units to use when printing sizes;
   if negative, the human-readable base.  */
static int output_block_size;

/* If nonzero, invoke the `sync' system call before getting any usage data.
   Using this option can make df very slow, especially with many or very
   busy disks.  Note that this may make a difference on some systems --
   SunOs4.1.3, for one.  It is *not* necessary on Linux.  */
static int require_sync = 0;

/* A filesystem type to display. */

struct fs_type_list
{
  char *fs_name;
  struct fs_type_list *fs_next;
};

/* Linked list of filesystem types to display.
   If `fs_select_list' is NULL, list all types.
   This table is generated dynamically from command-line options,
   rather than hardcoding into the program what it thinks are the
   valid filesystem types; let the user specify any filesystem type
   they want to, and if there are any filesystems of that type, they
   will be shown.

   Some filesystem types:
   4.2 4.3 ufs nfs swap ignore io vm efs dbg */

static struct fs_type_list *fs_select_list;

/* Linked list of filesystem types to omit.
   If the list is empty, don't exclude any types.  */

static struct fs_type_list *fs_exclude_list;

/* Linked list of mounted filesystems. */
static struct mount_entry *mount_list;

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  SYNC_OPTION = CHAR_MAX + 1,
  NO_SYNC_OPTION,
  BLOCK_SIZE_OPTION
};

#ifdef _AIX
 #pragma alloca
#endif

int process_arguments (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
void print_help (void);
void print_usage (void);

int w_df = -1;
int c_df = -1;
float w_dfp = -1.0;
float c_dfp = -1.0;
char *path = "";
char *exclude_device = "";
int verbose = 0;
int erronly = FALSE;
int display_mntp = FALSE;


int
main (int argc, char **argv)
{
	int usp = -1;
	int total_disk = -1;
	int used_disk = -1;
	int free_disk = -1;
	int result = STATE_UNKNOWN;
	int disk_result = STATE_UNKNOWN;
	char *command_line = "";
	char input_buffer[MAX_INPUT_BUFFER];
	char file_system[MAX_INPUT_BUFFER];
	char mntp[MAX_INPUT_BUFFER];
	char *output = "";

	struct fs_usage fsp;
	char *disk;

	if (process_arguments (argc, argv) != OK)
		usage ("Could not parse arguments\n");

	get_fs_usage (path, disk, &fsp);

	usp = (fsp.fsu_blocks - fsp.fsu_bavail) / fsp.fsu_blocks;
	disk_result = check_disk (usp, fsp.fsu_bavail);
	result = disk_result;
	asprintf (&output, "%llu of %llu kB (%2.0f%%) free (%d-byte blocks)",
	          fsp.fsu_bavail*fsp.fsu_blocksize/1024,
	          fsp.fsu_blocks*fsp.fsu_blocksize/1024,
	          (double)fsp.fsu_bavail*100/fsp.fsu_blocks,
	          fsp.fsu_blocksize);

	terminate (result, "DISK %s %s\n", state_text (result), output);
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"timeout", required_argument, 0, 't'},
		{"path", required_argument, 0, 'p'},
		{"partition", required_argument, 0, 'p'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"errors-only", no_argument, 0, 'e'},
		{"help", no_argument, 0, 'h'},
		{"mountpoint", no_argument, 0, 'm'},
		{"exclude_device", required_argument, 0, 'x'},
		{"quiet", no_argument, 0, 'q'},

		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "+?Vqhvet:c:w:p:x:m", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				w_df = atoi (optarg);
				break;
			}
			else if (strpbrk (optarg, ",:") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%d%*[:,]%f%%", &w_df, &w_dfp) == 2) {
				break;
			}
			else if (strstr (optarg, "%") && sscanf (optarg, "%f%%", &w_dfp) == 1) {
				break;
			}
			else {
				usage ("Warning threshold must be integer or percentage!\n");
			}
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				c_df = atoi (optarg);
				break;
			}
			else if (strpbrk (optarg, ",:") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%d%*[,:]%f%%", &c_df, &c_dfp) == 2) {
				break;
			}
			else if (strstr (optarg, "%") && sscanf (optarg, "%f%%", &c_dfp) == 1) {
				break;
			}
			else {
				usage ("Critical threshold must be integer or percentage!\n");
			}
		case 't':									/* timeout period */
			if (is_integer (optarg)) {
				timeout_interval = atoi (optarg);
				break;
			}
			else {
				usage ("Timeout Interval must be an integer!\n");
			}
		case 'p':									/* path or partition */
			path = optarg;
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 'q':									/* verbose */
			verbose--;
			break;
		case 'e':
			erronly = TRUE;
			break;
		case 'm': /* display mountpoint */
			display_mntp = TRUE;
			break;
 		case 'x':									/* exclude path or partition */
 			exclude_device = optarg;
 			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("check_disk: unrecognized option\n");
			break;
		}
	}

	c = optind;
	if (w_dfp == -1 && argc > c && is_intnonneg (argv[c]))
		w_dfp = (100.0 - atof (argv[c++]));

	if (c_dfp == -1 && argc > c && is_intnonneg (argv[c]))
		c_dfp = (100.0 - atof (argv[c++]));

	if (argc > c && strlen (path) == 0)
		path = argv[c++];

	return validate_arguments ();
}

int
validate_arguments ()
{
	if (w_df < 0 && c_df < 0 && w_dfp < 0 && c_dfp < 0) {
		printf ("INPUT ERROR: Unable to parse command line\n");
		return ERROR;
	}
	else if ((w_dfp >= 0 || c_dfp >= 0)
					 && (w_dfp < 0 || c_dfp < 0 || w_dfp > 100 || c_dfp > 100
							 || c_dfp > w_dfp)) {
		printf
			("INPUT ERROR: C_DFP (%f) should be less than W_DFP (%f) and both should be between zero and 100 percent, inclusive\n",
			 c_dfp, w_dfp);
		return ERROR;
	}
	else if ((w_df > 0 || c_df > 0) && (w_df < 0 || c_df < 0 || c_df > w_df)) {
		printf
			("INPUT ERROR: C_DF (%d) should be less than W_DF (%d) and both should be greater than zero\n",
			 c_df, w_df);
		return ERROR;
	}
	else {
		return OK;
	}
}

int
check_disk (usp, free_disk)
{
	int result = STATE_UNKNOWN;
	/* check the percent used space against thresholds */
	if (usp >= 0 && usp >= (100.0 - c_dfp))
		result = STATE_CRITICAL;
	else if (c_df >= 0 && free_disk <= c_df)
		result = STATE_CRITICAL;
	else if (usp >= 0 && usp >= (100.0 - w_dfp))
		result = STATE_WARNING;
	else if (w_df >= 0 && free_disk <= w_df)
		result = STATE_WARNING;
	else if (usp >= 0.0)
		result = STATE_OK;
	return result;
}

void
print_help (void)
{
	print_revision (progname, revision);
	printf ("Copyright (c) %s %s\n\t<%s>\n\n%s\n",
	         copyright, authors, email, summary);
	print_usage ();
	printf ("\nOptions:\n");
	printf (options);
	support ();
}

void
print_usage (void)
{
	printf
		("Usage: %s %s\n"
		 "       %s (-h|--help)\n"
		 "       %s (-V|--version)\n", progname, option_summary, progname, progname);
}
