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
long unsigned int warn_size = 0;
long unsigned int crit_size = 0;
int verbose;
int allswaps;

#if !defined(sun)
int sun = 0;	/* defined by compiler if it is a sun solaris system */
#endif

int
main (int argc, char **argv)
{
	int percent_used, percent;
	long unsigned int total_swap = 0, used_swap = 0, free_swap = 0;
	long unsigned int total, used, free;
	int conv_factor;		/* Convert to MBs */
	int result = STATE_OK;
	char input_buffer[MAX_INPUT_BUFFER];
#ifdef HAVE_SWAP
	char *temp_buffer;
	char *swap_command;
	char *swap_format;
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
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		if (sscanf (input_buffer, " %s %lu %lu %lu", str, &total, &used, &free) == 4 &&
		    strstr (str, "Swap")) {
			total = total / 1048576;
			used = used / 1048576;
			free = free / 1048576;
#endif
#ifdef HAVE_SWAP
	if (!allswaps && sun) {
		asprintf(&swap_command, "%s", "/usr/sbin/swap -s");
		asprintf(&swap_format, "%s", "%*s %*dk %*s %*s + %*dk %*s = %dk %*s %dk %*s");
		conv_factor = 2048;
	} else {
		asprintf(&swap_command, "%s", SWAP_COMMAND);
		asprintf(&swap_format, "%s", SWAP_FORMAT);
		conv_factor = SWAP_CONVERSION;
	}

	if (verbose >= 2)
		printf ("Command: %s\n", swap_command);
	if (verbose >= 3)
		printf ("Format: %s\n", swap_format);

	child_process = spopen (swap_command);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", swap_command);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", swap_command);

	sprintf (str, "%s", "");
	/* read 1st line */
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	if (strcmp (swap_format, "") == 0) {
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

	if (!allswaps && sun) {
		sscanf (input_buffer, swap_format, &used_swap, &free_swap);
		used_swap = used_swap / 1024;
		free_swap = free_swap / 1024;
		total_swap = used_swap + free_swap;
	} else {
		while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
			sscanf (input_buffer, swap_format, &total, &free);

			total = total / conv_factor;
			free = free / conv_factor;
			if (verbose >= 3)
				printf ("total=%d, free=%d\n", total, free);

			used = total - free;
#endif
			total_swap += total;
			used_swap += used;
			free_swap += free;
			if (allswaps) {
				percent = 100 * (((double) used) / ((double) total));
				result = max_state (result, check_swap (percent, free));
				if (verbose)
					asprintf (&status, "%s [%lu (%d%%)]", status, free, 100 - percent);
			}
		}
	}
	percent_used = 100 * ((double) used_swap) / ((double) total_swap);
	result = max_state (result, check_swap (percent_used, free_swap));
	asprintf (&status, " %d%% free (%lu MB out of %lu MB)%s",
						(100 - percent_used), free_swap, total_swap, status);

#ifdef HAVE_PROC_MEMINFO
	fclose(fp);
#endif
#ifdef HAVE_SWAP
	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);
#endif

	terminate (result, "SWAP %s:%s\n", state_text (result), status);
}




int
check_swap (int usp, int free_swap)
{
	int result = STATE_UNKNOWN;
	if (usp >= 0 && usp >= (100.0 - crit_percent))
		result = STATE_CRITICAL;
	else if (crit_size >= 0 && free_swap <= crit_size)
		result = STATE_CRITICAL;
	else if (usp >= 0 && usp >= (100.0 - warn_percent))
		result = STATE_WARNING;
	else if (warn_size >= 0 && free_swap <= warn_size)
		result = STATE_WARNING;
	else if (usp >= 0.0)
		result = STATE_OK;
	return result;
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 0;  /* option character */
	int wc = 0; /* warning counter  */
	int cc = 0; /* critical counter */

	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"allswaps", no_argument, 0, 'a'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "+?Vvhac:w:", long_options, &option_index);

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
							 sscanf (optarg, "%lu,%d%%", &warn_size, &warn_percent) == 2) {
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
							 sscanf (optarg, "%lu,%d%%", &crit_size, &crit_percent) == 2) {
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
		case 'a':									/* all swap */
			allswaps = TRUE;
			break;
		case 'v':									/* verbose */
			verbose++;
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
	else if (warn_percent < crit_percent) {
		usage
			("Warning percentage should be more than critical percentage\n");
	}
	else if (warn_size < crit_size) {
		usage
			("Warning free space should be more than critical free space\n");
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
		 "   Exit with WARNING status if less than PERCENT of swap space has been used\n"
		 " -c, --critical=INTEGER\n"
		 "   Exit with CRITICAL status if less than INTEGER bytes of swap space are free\n"
		 " -c, --critical=PERCENT%%\n"
		 "   Exit with CRITCAL status if less than PERCENT of swap space has been used\n"
		 " -a, --allswaps\n"
		 "    Conduct comparisons for all swap partitions, one by one\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n"
#ifdef sun
		 "\nOn Solaris, if -a specified, uses swap -l, otherwise uses swap -s.\n"
		 "Will be discrepencies because swap -s counts allocated swap and includes real memory\n"
#endif
		 "\n"
		 );
	support ();
}
