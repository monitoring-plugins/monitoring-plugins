/******************************************************************************
*
* CHECK_SWAP.C
*
* Program: Process plugin for Nagios
* License: GPL
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
*
* $Id$
*
******************************************************************************/

#include "common.h"
#include "popen.h"
#include "utils.h"

#define PROGNAME "check_swap"

int process_arguments (int argc, char **argv);
int call_getopt (int argc, char **argv);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

int warn_percent = 200, crit_percent = 200, warn_size = -1, crit_size = -1;

int
main (int argc, char **argv)
{
	int total_swap, used_swap, free_swap, percent_used;
	int result = STATE_OK;
	char input_buffer[MAX_INPUT_BUFFER];
#ifdef HAVE_SWAP
	char *temp_buffer;
#endif
#ifdef HAVE_PROC_MEMINFO
	FILE *fp;
#endif
	char str[32];
	char *status = NULL;

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

#ifdef HAVE_PROC_MEMINFO
	fp = fopen (PROC_MEMINFO, "r");
	status = ssprintf (status, "%s", "Swap used:");
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		sscanf (input_buffer, " %s %d %d %d", str, &total_swap, &used_swap,
						&free_swap);
		if (strstr (str, "Swap")) {
			percent_used = 100 * (((float) used_swap) / ((float) total_swap));
			status = ssprintf
				(status,
				 "%s %2d%% (%d bytes out of %d)",
				 status, percent_used, used_swap, total_swap);
			if (percent_used >= crit_percent || free_swap <= crit_size)
				result = STATE_CRITICAL;
			else if (percent_used >= warn_percent || free_swap <= warn_size)
				result = STATE_WARNING;
			break;
		}
	}
	fclose (fp);
#else
#ifdef HAVE_SWAP
	child_process = spopen (SWAP_COMMAND);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", SWAP_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", SWAP_COMMAND);

	sprintf (str, "%s", "");
	/* read 1st line */
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	if (strcmp (SWAP_FORMAT, "") == 0) {
		temp_buffer = strtok (input_buffer, " \n");
		while (temp_buffer) {
			if (strstr (temp_buffer, "blocks"))
				sprintf (str, "%s %s", str, "%f");
			else if (strstr (temp_buffer, "free"))
				sprintf (str, "%s %s", str, "%f");
			else
				sprintf (str, "%s %s", str, "%*s");
			temp_buffer = strtok (NULL, " \n");
		}
	}

	status = ssprintf (status, "%s", "Swap used:");
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		sscanf (input_buffer, SWAP_FORMAT, &total_swap, &free_swap);
		used_swap = total_swap - free_swap;
		percent_used = 100 * ((float) used_swap) / ((float) total_swap);
		status = ssprintf
			(status,
			 "%s %2d%% (%d bytes out of %d)",
			 status, percent_used, used_swap, total_swap);
		if (percent_used >= crit_percent || free_swap <= crit_size)
			result = STATE_CRITICAL;
		else if (percent_used >= warn_percent || free_swap <= warn_size)
			result = STATE_WARNING;
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max (result, STATE_WARNING);
#endif
#endif

#ifndef SWAP_COMMAND
#ifndef SWAP_FILE
#ifndef HAVE_PROC_MEMINFO
	return STATE_UNKNOWN;
#endif
#endif
#endif

	if (result == STATE_OK)
		printf ("Swap ok - %s\n", status);
	else if (result == STATE_CRITICAL)
		printf ("CRITICAL - %s\n", status);
	else if (result == STATE_WARNING)
		printf ("WARNING - %s\n", status);
	else if (result == STATE_UNKNOWN)
		printf ("Unable to read output\n");
	else {
		result = STATE_UNKNOWN;
		printf ("UNKNOWN - %s\n", status);
	}

	return result;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	if (argc < 2)
		return ERROR;

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "+?Vhc:w:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?Vhc:w:");
#endif

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warn_size = atoi (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%d,%d%%", &warn_size, &warn_percent) == 2) {
				break;
			}
			else if (strstr (optarg, "%") &&
							 sscanf (optarg, "%d%%", &warn_percent) == 1) {
				break;
			}
			else {
				usage ("Warning threshold must be integer or percentage!\n");
			}
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				crit_size = atoi (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%d,%d%%", &crit_size, &crit_percent) == 2) {
				break;
			}
			else if (strstr (optarg, "%") &&
							 sscanf (optarg, "%d%%", &crit_percent) == 1) {
				break;
			}
			else {
				usage ("Critical threshold must be integer or percentage!\n");
			}
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("Invalid argument\n");
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();
	if (warn_percent > 100 && is_intnonneg (argv[c]))
		warn_percent = atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (crit_percent > 100 && is_intnonneg (argv[c]))
		crit_percent = atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (warn_size < 0 && is_intnonneg (argv[c]))
		warn_size = atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (crit_size < 0 && is_intnonneg (argv[c]))
		crit_size = atoi (argv[c++]);

	return validate_arguments ();
}





int
validate_arguments (void)
{
	if (warn_percent > 100 && crit_percent > 100 && warn_size < 0
			&& crit_size < 0) {
		return ERROR;
	}
	else if (warn_percent > crit_percent) {
		usage
			("Warning percentage should not be less than critical percentage\n");
	}
	else if (warn_size < crit_size) {
		usage
			("Warning free space should not be more than critical free space\n");
	}
	return OK;
}





void
print_usage (void)
{
	printf
		("Usage: check_swap -w <used_percentage>%% -c <used_percentage>%%\n"
		 "       check_swap -w <bytes_free> -c <bytes_free>\n"
		 "       check_swap (-V|--version)\n" "       check_swap (-h|--help)\n");
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 2000 Karl DeBisschop\n\n"
		 "This plugin will check all of the swap partitions and return an\n"
		 "error if the the avalable swap space is less than specified.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -w, --warning=INTEGER\n"
		 "   Exit with WARNING status if less than INTEGER bytes of swap space are free\n"
		 " -w, --warning=PERCENT%%\n"
		 "   Exit with WARNING status if more than PERCENT of swap space has been used\n"
		 " -c, --critical=INTEGER\n"
		 "   Exit with CRITICAL status if less than INTEGER bytes of swap space are free\n"
		 " -c, --critical=PERCENT%%\n"
		 "   Exit with CRITCAL status if more than PERCENT of swap space has been used\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n\n");
	support ();
}
