/******************************************************************************
 *
 * Program: Swap space plugin for Nagios
 * License: GPL
 *
 * License Information:
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
 * Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
 *
 * $Id$
 *
 *****************************************************************************/

#include "common.h"
#include "popen.h"
#include "utils.h"

const char *progname = "check_swap";
#define REVISION "$Revision$"
#define COPYRIGHT "2000-2002"
#define AUTHOR "Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Check swap space on local server.\n"

int process_arguments (int argc, char **argv);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

int warn_percent = 200;
int crit_percent = 200;
int warn_size = -1;
int crit_size = -1;
int verbose;
int allswaps;

int
main (int argc, char **argv)
{
	int total_swap = 0, used_swap = 0, free_swap = 0, percent_used;
	int total, used, free, percent;
	int result = STATE_OK;
	char input_buffer[MAX_INPUT_BUFFER];
#ifdef HAVE_SWAP
	char *temp_buffer;
#endif
#ifdef HAVE_PROC_MEMINFO
	FILE *fp;
#endif
	char str[32];
	char *status = "";

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

#ifdef HAVE_PROC_MEMINFO
	fp = fopen (PROC_MEMINFO, "r");
	asprintf (&status, "%s", "Swap used:");
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		if (sscanf (input_buffer, " %s %d %d %d", str, &total, &used, &free) == 4 &&
		    strstr (str, "Swap")) {
			total_swap += total;
			used_swap += used;
			free_swap += free;
			if (allswaps) {
				percent = 100 * (((float) used) / ((float) total));
				if (percent >= crit_percent || free <= crit_size)
					result = max_state (STATE_CRITICAL, result);
				else if (percent >= warn_percent || free <= warn_size)
					result = max_state (STATE_WARNING, result);
				if (verbose)
					asprintf (&status, "%s [%d/%d]", status, used, total);
			}
		}
	}
	percent_used = 100 * (((float) used_swap) / ((float) total_swap));
	if (percent_used >= crit_percent || free_swap <= crit_size)
		result = max_state (STATE_CRITICAL, result);
	else if (percent_used >= warn_percent || free_swap <= warn_size)
		result = max_state (STATE_WARNING, result);
	asprintf (&status, "%s %2d%% (%d out of %d)", status, percent_used,
	          used_swap, total_swap);
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

	asprintf (&status, "%s", "Swap used:");
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		sscanf (input_buffer, SWAP_FORMAT, &total, &free);
		used = total - free;
		total_swap += total;
		used_swap += used;
		free_swap += free;
		if (allswaps) {
			percent = 100 * (((float) used) / ((float) total));
			if (percent >= crit_percent || free <= crit_size)
				result = max_state (STATE_CRITICAL, result);
			else if (percent >= warn_percent || free <= warn_size)
				result = max_state (STATE_WARNING, result);
			if (verbose)
				asprintf (&status, "%s [%d/%d]", status, used, total);
		}
	}
	percent_used = 100 * ((float) used_swap) / ((float) total_swap);
	asprintf (&status, "%s %2d%% (%d out of %d)",
						status, percent_used, used_swap, total_swap);
	if (percent_used >= crit_percent || free_swap <= crit_size)
		result = max_state (STATE_CRITICAL, result);
	else if (percent_used >= warn_percent || free_swap <= warn_size)
		result = max_state (STATE_WARNING, result);

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);
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
	int c = 0;  /* option character */
	int wc = 0; /* warning counter  */
	int cc = 0; /* critical counter */

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"all", no_argument, 0, 'a'},
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
		c = getopt_long (argc, argv, "+?Vvhac:w:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?Vvhac:w:");
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
			wc++;
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
			cc++;
		case 'a':									/* verbose */
			allswaps = TRUE;
			break;
		case 'v':									/* verbose */
			verbose = TRUE;
			break;
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
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
		("Usage:\n"
		 " %s [-a] -w <used_percentage>%% -c <used_percentage>%%\n"
		 " %s [-a] -w <bytes_free> -c <bytes_free>\n"
		 " %s (-h | --help) for detailed help\n"
		 " %s (-V | --version) for version information\n",
		 progname, progname, progname, progname);
}





void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n", COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
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
		 " -a, --allswaps\n"
		 "    Conduct comparisons for all swap partitions, one by one\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n\n");
	support ();
}
