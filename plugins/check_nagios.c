/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

******************************************************************************/

const char *progname = "check_nagios";
const char *revision = "$Revision$";
const char *copyright = "1999-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

void
print_usage (void)
{
	printf (_("\
Usage: %s -F <status log file> -e <expire_minutes> -C <process_string>\n"),
	        progname);
}

void
print_help (void)
{
	print_revision (progname, revision);

	printf (_(COPYRIGHT), copyright, email);

	printf (_("\
This plugin attempts to check the status of the Nagios process on the local\n\
machine. The plugin will check to make sure the Nagios status log is no older\n\
than the number of minutes specified by the <expire_minutes> option.  It also\n\
uses the /bin/ps command to check for a process matching whatever you specify\n\
by the <process_string> argument.\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
-F, --filename=FILE\n\
   Name of the log file to check\n\
-e, --expires=INTEGER\n\
   Seconds aging afterwhich logfile is condsidered stale\n\
-C, --command=STRING\n\
   Command to search for in process table\n"));

	printf (_("\
Example:\n\
   ./check_nagios -e 5 \\\
   -F /usr/local/nagios/var/status.log \\\
   -C /usr/local/nagios/bin/nagios\n"));
}

int process_arguments (int, char **);

char *status_log = NULL;
char *process_string = NULL;
int expire_minutes = 0;

int verbose = 0;

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	char input_buffer[MAX_INPUT_BUFFER];
	unsigned long latest_entry_time = 0L;
	unsigned long temp_entry_time = 0L;
	int proc_entries = 0;
	time_t current_time;
	char *temp_ptr;
	FILE *fp;
	int procuid = 0;
	int procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	float procpcpu = 0;
	char procstat[8];
	char procprog[MAX_INPUT_BUFFER];
	char *procargs;
	int pos, cols;

	if (process_arguments (argc, argv) == ERROR)
		usage (_("Could not parse arguments\n"));

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		printf (_("Cannot catch SIGALRM"));
		return STATE_UNKNOWN;
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* open the status log */
	fp = fopen (status_log, "r");
	if (fp == NULL) {
		printf (_("Error: Cannot open status log for reading!\n"));
		return STATE_CRITICAL;
	}

	/* get the date/time of the last item updated in the log */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		temp_ptr = strtok (input_buffer, "]");
		temp_entry_time =
			(temp_ptr == NULL) ? 0L : strtoul (temp_ptr + 1, NULL, 10);
		if (temp_entry_time > latest_entry_time)
			latest_entry_time = temp_entry_time;
	}
	fclose (fp);

	/* run the command to check for the Nagios process.. */
	child_process = spopen (PS_COMMAND);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), PS_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), PS_COMMAND);
	}

	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);

	/* count the number of matching Nagios processes... */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		cols = sscanf (input_buffer, PS_FORMAT, PS_VARLIST);
		if ( cols >= 6 ) {
			asprintf (&procargs, "%s", input_buffer + pos);
			strip (procargs);
			
			if (!strstr(procargs, argv[0]) && strstr(procargs, process_string)) {
				proc_entries++;
				if (verbose)
					printf (_("Found process: %s\n"), procargs);
			}
		}
	}

	/* If we get anything on stderr, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	/* reset the alarm handler */
	alarm (0);

	if (proc_entries == 0) {
		printf (_("Could not locate a running Nagios process!\n"));
		return STATE_CRITICAL;
	}

	result = STATE_OK;

	time (&current_time);
	if ((int)(current_time - latest_entry_time) > (expire_minutes * 60))
		result = STATE_WARNING;

	printf
		(_("Nagios %s: located %d process%s, status log updated %d second%s ago\n"),
		 (result == STATE_OK) ? "ok" : "problem", proc_entries,
		 (proc_entries == 1) ? "" : "es",
		 (int) (current_time - latest_entry_time),
		 ((int) (current_time - latest_entry_time) == 1) ? "" : "s");

	return result;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
		{"filename", required_argument, 0, 'F'},
		{"expires", required_argument, 0, 'e'},
		{"command", required_argument, 0, 'C'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	if (!is_option (argv[1])) {
		status_log = argv[1];
		if (is_intnonneg (argv[2]))
			expire_minutes = atoi (argv[2]);
		else
			terminate (STATE_UNKNOWN,
								 _("Expiration time must be an integer (seconds)\nType '%s -h' for additional help\n"),
								 progname);
		process_string = argv[3];
		return OK;
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVvF:C:e:", long_options, &option_index);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf (_("%s: Unknown argument: %c\n\n"), progname, optopt);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'F':									/* status log */
			status_log = optarg;
			break;
		case 'C':									/* command */
			process_string = optarg;
			break;
		case 'e':									/* expiry time */
			if (is_intnonneg (optarg))
				expire_minutes = atoi (optarg);
			else
				terminate (STATE_UNKNOWN,
									 _("Expiration time must be an integer (seconds)\nType '%s -h' for additional help\n"),
									 progname);
			break;
		case 'v':
			verbose++;
			break;
		}
	}


	if (status_log == NULL)
		terminate (STATE_UNKNOWN,
							 _("You must provide the status_log\nType '%s -h' for additional help\n"),
							 progname);
	else if (process_string == NULL)
		terminate (STATE_UNKNOWN,
							 _("You must provide a process string\nType '%s -h' for additional help\n"),
							 progname);

	return OK;
}
