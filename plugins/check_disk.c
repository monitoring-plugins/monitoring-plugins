/******************************************************************************
 *
 * CHECK_DISK.C
 *
 * Program: Disk space plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
 *
 * $Id$
 *
 * Description:
 *
 * This plugin will use the /bin/df command to check the free space on
 * currently mounted filesystems.  If the percent used disk space is
 * above <c_dfp>, a STATE_CRITICAL is returned.  If the percent used
 * disk space is above <w_dfp>, a STATE_WARNING is returned.  If the
 * speicified filesystem cannot be read, a STATE_CRITICAL is returned,
 * other errors with reading the output result in a STATE_UNKNOWN
 * error.
 *
 * Notes:
 *  - IRIX support added by Charlie Cook 4-16-1999
 *  - Modifications by Karl DeBisschop 1999-11-24
 *     reformat code to 80 char screen width
 *     set STATE_WARNING if stderr is written or spclose status set
 *     set default result to STAT_UNKNOWN
 *     initailize usp to -1, eliminate 'found' variable
 *     accept any filename/filesystem
 *     use sscanf, drop while loop
 *
 *****************************************************************************/

#include "common.h"
#include "popen.h"
#include "utils.h"
#include <stdarg.h>

#define PROGNAME "check_disk"

int process_arguments (int, char **);
int call_getopt (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
void print_help (void);
void print_usage (void);

int w_df = -1;
int c_df = -1;
float w_dfp = -1.0;
float c_dfp = -1.0;
char *path = NULL;
int verbose = FALSE;

int
main (int argc, char **argv)
{
	int len;
	int usp = -1;
	int total_disk = -1;
	int used_disk = -1;
	int free_disk = -1;
	int result = STATE_UNKNOWN;
	char *command_line = NULL;
	char input_buffer[MAX_INPUT_BUFFER] = "";
	char file_system[MAX_INPUT_BUFFER] = "";
	char outbuf[MAX_INPUT_BUFFER] = "";
	char *output = NULL;

	if (process_arguments (argc, argv) != OK)
		usage ("Could not parse arguments\n");

	command_line = ssprintf (command_line, "%s %s", DF_COMMAND, path);

	if (verbose)
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

		if (sscanf
				(input_buffer, "%s %d %d %d %d%%", file_system, &total_disk,
				 &used_disk, &free_disk, &usp) == 5
				|| sscanf (input_buffer, "%s %*s %d %d %d %d%%", file_system,
									 &total_disk, &used_disk, &free_disk, &usp) == 5) {
			result = max (result, check_disk (usp, free_disk));
			len =
				snprintf (outbuf, MAX_INPUT_BUFFER - 1,
									" [%d kB (%d%%) free on %s]", free_disk, 100 - usp,
									file_system);
			outbuf[len] = 0;
			output = strscat (output, outbuf);
		}
		else {
			printf ("Unable to read output:\n%s\n%s\n", command_line, input_buffer);
			return result;
		}
	}

	/* If we get anything on stderr, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max (result, STATE_WARNING);

	else if (usp < 0)
		printf ("Disk %s not mounted or nonexistant\n", argv[3]);
	else if (result == STATE_UNKNOWN)
		printf ("Unable to read output\n%s\n%s\n", command_line, input_buffer);
	else
		printf ("DISK %s -%s\n", state_text (result), output);

	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0) {
			strcpy (argv[c], "-t");
		}
	}

	c = 0;
	while ((c += (call_getopt (argc - c, &argv[c]))) < argc) {

		if (w_dfp == -1 && is_intnonneg (argv[c]))
			w_dfp = (100.0 - atof (argv[c]));
		else if (c_dfp == -1 && is_intnonneg (argv[c]))
			c_dfp = (100.0 - atof (argv[c]));
		else if (path == NULL || path[0] == 0)
			path = strscpy (path, argv[c]);
	}

	if (path == NULL) {
		path = malloc (1);
		if (path == NULL)
			terminate (STATE_UNKNOWN, "Could not malloc empty path\n");
		path[0] = 0;
	}

	return validate_arguments ();
}

int
call_getopt (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"timeout", required_argument, 0, 't'},
		{"path", required_argument, 0, 'p'},
		{"partition", required_argument, 0, 'p'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+?Vhvt:c:w:p:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?Vhvt:c:w:p:");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 't':
		case 'c':
		case 'w':
		case 'p':
			i++;
		}

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
			verbose = TRUE;
			break;
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("check_disk: unrecognized option\n");
			break;
		}
	}
	return i;
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
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 2000 Ethan Galstad/Karl DeBisschop\n\n"
		 "This plugin will check the percent of used disk space on a mounted\n"
		 "file system and generate an alert if percentage is above one of the\n"
		 "threshold values.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -w, --warning=INTEGER\n"
		 "   Exit with WARNING status if less than INTEGER kilobytes of disk are free\n"
		 " -w, --warning=PERCENT%%\n"
		 "   Exit with WARNING status if more than PERCENT of disk space is free\n"
		 " -c, --critical=INTEGER\n"
		 "   Exit with CRITICAL status if less than INTEGER kilobytes of disk are free\n"
		 " -c, --critical=PERCENT%%\n"
		 "   Exit with CRITCAL status if more than PERCENT of disk space is free\n"
		 " -p, --path=PATH, --partition=PARTTION\n"
		 "    Path or partition (checks all mounted partitions if unspecified)\n"
		 " -v, --verbose\n"
		 "    Show details for command-line debugging (do not use with nagios server)\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n\n");
	support ();
}

void
print_usage (void)
{
	printf
		("Usage: %s -w limit -c limit [-p path] [-t timeout] [--verbose]\n"
		 "       %s (-h|--help)\n"
		 "       %s (-V|--version)\n", PROGNAME, PROGNAME, PROGNAME);
}
