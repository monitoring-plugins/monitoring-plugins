/******************************************************************************
 *
 * CHECK_USERS.C
 *
 * Program: Current users plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: $Date$
 * Modifications: 
 *
 * 1999-11-17 Karl DeBisschop
 *  - check stderr and status from spoen/spclose
 *  - reformat commenst to fit 80-cahr screen
 *  - set default result to STATE_UNKNOWN
 *  - initialize users at -1, eliminate 'found' variable
 *
 * Command line: CHECK_USERS <wusers> <cusers>
 *
 * Description:
 *
 * This plugin will use the /usr/bin/who command to check the number
 * of users currently logged into the system.  If number of logged in
 * user exceeds the number specified by the <cusers> option, a
 * STATE_CRITICAL is return.  It it exceeds <wusers>, a STATE_WARNING
 * is returned.  Errors reading the output from the who command result
 * in a STATE_UNKNOWN error.
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
 *****************************************************************************/

#include "common.h"
#include "popen.h"
#include "utils.h"

const char *progname = "check_users";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2002"
#define AUTHOR "Ethan Galstad"
#define EMAIL "nagios@nagios.org"

#define possibly_set(a,b) ((a) == 0 ? (b) : 0)

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

int wusers = -1;
int cusers = -1;

int
main (int argc, char **argv)
{
	int users = -1;
	int result = STATE_OK;
	char input_buffer[MAX_INPUT_BUFFER];

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

	/* run the command */
	child_process = spopen (WHO_COMMAND);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", WHO_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", WHO_COMMAND);

	users = 0;

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* increment 'users' on all lines except total user count */
		if (input_buffer[0] != '#') {
			users++;
			continue;
		}

		/* get total logged in users */
		if (sscanf (input_buffer, "# users=%d", &users) == 1)
			break;

	}

	/* check STDERR */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = possibly_set (result, STATE_UNKNOWN);
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = possibly_set (result, STATE_UNKNOWN);

	/* else check the user count against warning and critical thresholds */
	if (users >= cusers)
		result = STATE_CRITICAL;
	else if (users >= wusers)
		result = STATE_WARNING;
	else if (users >= 0)
		result = STATE_OK;

	if (result == STATE_UNKNOWN)
		printf ("Unable to read output\n");
	else
		printf ("USERS %s - %d users currently logged in\n", state_text (result),
						users);

	return result;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "+hVvc:w:", long_options, &option_index);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf ("%s: Unknown argument: %s\n\n", progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 'c':									/* critical */
			if (!is_intnonneg (optarg))
				usage ("Critical threshold must be a nonnegative integer\n");
			cusers = atoi (optarg);
			break;
		case 'w':									/* warning */
			if (!is_intnonneg (optarg))
				usage ("Warning threshold must be a nonnegative integer\n");
			wusers = atoi (optarg);
			break;
		}
	}

	c = optind;
	if (wusers == -1 && argc > c) {
		if (is_intnonneg (argv[c]) == FALSE)
			usage ("Warning threshold must be a nonnegative integer\n");
		wusers = atoi (argv[c++]);
	}

	if (cusers == -1 && argc > c) {
		if (is_intnonneg (argv[c]) == FALSE)
			usage ("Warning threshold must be a nonnegative integer\n");
		cusers = atoi (argv[c]);
	}

	return OK;
}





void
print_usage (void)
{
	printf ("Usage: %s -w <users> -c <users>\n", progname);
}





void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) " COPYRIGHT " " AUTHOR "(" EMAIL ")\n\n"
		 "This plugin checks the number of users currently logged in on the local\n"
		 "system and generates an error if the number exceeds the thresholds specified.\n");
	print_usage ();
	printf
		("Options:\n"
		 " -w, --warning=INTEGER\n"
		 "    Set WARNING status if more than INTEGER users are logged in\n"
		 " -c, --critical=INTEGER\n"
		 "    Set CRITICAL status if more than INTEGER users are logged in\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n" "    Print version information\n");
}
