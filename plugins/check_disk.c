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
#include "popen.h"
#include "utils.h"
#include <stdarg.h>

#ifdef _AIX
 #pragma alloca
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
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

#ifdef HAVE_STRUCT_STATFS
#ifdef HAVE_SYS_VFS_H
#include <sys/vfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif
	struct statfs buf;
#endif

	if (process_arguments (argc, argv) != OK)
		usage ("Could not parse arguments\n");

#ifdef HAVE_STRUCT_STATFS

	if (statfs (path, &buf) == -1) {
		switch (errno)
			{
#ifdef ENOTDIR
			case ENOTDIR:
				terminate (STATE_UNKNOWN, "A component of the path prefix is not a directory.\n");
#endif
#ifdef ENAMETOOLONG
			case ENAMETOOLONG:
				terminate (STATE_UNKNOWN, "path is too long.\n");
#endif
#ifdef ENOENT
			case ENOENT:
				terminate (STATE_UNKNOWN, "The file referred to by path does not exist.\n");
#endif
#ifdef EACCES
			case EACCES:
				terminate (STATE_UNKNOWN, "Search permission is denied for a component of the path prefix of path.\n");
#endif
#ifdef ELOOP
			case ELOOP:
				terminate (STATE_UNKNOWN, "Too many symbolic links were encountered in translating path.\n");
#endif
#ifdef EFAULT
			case EFAULT:
				terminate (STATE_UNKNOWN, "Buf or path points to an invalid address.\n");
#endif
#ifdef EIO
			case EIO:
				terminate (STATE_UNKNOWN, "An I/O error occurred while reading from or writing to the file system.\n");
#endif
#ifdef ENOMEM
			case ENOMEM:
				terminate (STATE_UNKNOWN, "Insufficient kernel memory was available.\n");
#endif
#ifdef ENOSYS
			case ENOSYS:
				terminate (STATE_UNKNOWN, "The  filesystem path is on does not support statfs.\n");
#endif
			}
	}

	usp = (buf.f_blocks - buf.f_bavail) / buf.f_blocks;
	disk_result = check_disk (usp, buf.f_bavail);
	result = disk_result;
	asprintf (&output, "%ld of %ld kB free (%ld-byte blocks)",
	          buf.f_bavail*buf.f_bsize/1024, buf.f_blocks*buf.f_bsize/1024, buf.f_bsize);

#else

	asprintf (&command_line, "%s %s", DF_COMMAND, path);

	if (verbose>0)
		printf ("%s ==> ", command_line);

	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", command_line);
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		if (!index (input_buffer, '/'))
			continue;

		/* Fixes AIX /proc fs which lists - for size values */
		if (strstr (input_buffer, "/proc ") == input_buffer)
			continue;

		if (sscanf (input_buffer, "%s %d %d %d %d%% %s", file_system,
		     &total_disk, &used_disk, &free_disk, &usp, mntp) == 6 ||
		    sscanf (input_buffer, "%s %*s %d %d %d %d%% %s", file_system,
				 &total_disk, &used_disk, &free_disk, &usp, mntp) == 6) {

 			if (strcmp(exclude_device,file_system) == 0 ||
			    strcmp(exclude_device,mntp) == 0) {
 				if (verbose>0)
 					printf ("ignoring %s.", file_system);
				continue;
 			}

			disk_result = check_disk (usp, free_disk);

			if (strcmp (file_system, "none") == 0)
				strncpy (file_system, mntp, MAX_INPUT_BUFFER-1);

			if (disk_result==STATE_OK && erronly && !verbose)
				continue;

			if (disk_result!=STATE_OK || verbose>=0) 
				asprintf (&output, "%s [%d kB (%d%%) free on %s]", output,
				          free_disk, 100 - usp, display_mntp ? mntp : file_system);

			result = max_state (result, disk_result);
		}

		else {
			printf ("Unable to read output:\n%s\n%s\n", command_line, input_buffer);
			return result;
		}

	}

	/* If we get anything on stderr, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (result != STATE_CRITICAL) {
			result = STATE_WARNING;
		}
	}

	/* close stderr */
	if (child_stderr) 
		(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose(child_process)!=0 && result!=STATE_CRITICAL)
			result = STATE_WARNING;

	if (usp < 0)
		terminate (result, "Disk \"%s\" not mounted or nonexistant\n", path);
	else if (result == STATE_UNKNOWN)
		terminate (result, "Unable to read output\n%s\n%s\n", command_line, input_buffer);

#endif

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
