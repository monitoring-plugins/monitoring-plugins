/*****************************************************************************
* 
* Nagios check_nagios plugin
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_nagios plugin
* 
* This plugin checks the status of the Nagios process on the local machine.
* The plugin will check to make sure the Nagios status log is no older than
* the number of minutes specified by the expires option.
* It also checks the process table for a process matching the command
* argument.
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

const char *progname = "check_nagios";
const char *copyright = "1999-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "runcmd.h"
#include "utils.h"

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

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
	int procpid = 0;
	int procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	float procpcpu = 0;
	char procstat[8];
#ifdef PS_USES_PROCETIME
	char procetime[MAX_INPUT_BUFFER];
#endif /* PS_USES_PROCETIME */
	char procprog[MAX_INPUT_BUFFER];
	char *procargs;
	int pos, cols;
	int expected_cols = PS_COLS - 1;
	const char *zombie = "Z";
	char *temp_string;
	output chld_out, chld_err;
	size_t i;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* open the status log */
	fp = fopen (status_log, "r");
	if (fp == NULL) {
		die (STATE_CRITICAL, "NAGIOS %s: %s\n", _("CRITICAL"), _("Cannot open status log for reading!"));
	}

	/* get the date/time of the last item updated in the log */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		if ((temp_ptr = strstr (input_buffer, "created=")) != NULL) {
			temp_entry_time = strtoul (temp_ptr + 8, NULL, 10);
			latest_entry_time = temp_entry_time;
			break;
		} else if ((temp_ptr = strtok (input_buffer, "]")) != NULL) {
			temp_entry_time = strtoul (temp_ptr + 1, NULL, 10);
			if (temp_entry_time > latest_entry_time)
				latest_entry_time = temp_entry_time;
		}
	}
	fclose (fp);

	if (verbose >= 2)
		printf("command: %s\n", PS_COMMAND);

	/* run the command to check for the Nagios process.. */
	if((result = np_runcmd(PS_COMMAND, &chld_out, &chld_err, 0)) != 0)
		result = STATE_WARNING;

	/* count the number of matching Nagios processes... */
	for(i = 0; i < chld_out.lines; i++) {
		cols = sscanf (chld_out.line[i], PS_FORMAT, PS_VARLIST);
		/* Zombie processes do not give a procprog command */
		if ( cols == (expected_cols - 1) && strstr(procstat, zombie) ) {
			cols = expected_cols;
			/* Set some value for procargs for the strip command further below
			 * Seen to be a problem on some Solaris 7 and 8 systems */
			chld_out.line[i][pos] = '\n';
			chld_out.line[i][pos+1] = 0x0;
		}
		if ( cols >= expected_cols ) {
			xasprintf (&procargs, "%s", chld_out.line[i] + pos);
			strip (procargs);

			/* Some ps return full pathname for command. This removes path */
			temp_string = strtok ((char *)procprog, "/");
			while (temp_string) {
				strcpy(procprog, temp_string);
				temp_string = strtok (NULL, "/");
			}

			/* May get empty procargs */
			if (!strstr(procargs, argv[0]) && strstr(procargs, process_string) && strcmp(procargs,"")) {
				proc_entries++;
				if (verbose >= 2) {
					printf (_("Found process: %s %s\n"), procprog, procargs);
				}
			}
		}
	}

	/* If we get anything on stderr, at least set warning */
	if(chld_err.buflen)
		result = max_state (result, STATE_WARNING);

	/* reset the alarm handler */
	alarm (0);

	if (proc_entries == 0) {
		die (STATE_CRITICAL, "NAGIOS %s: %s\n", _("CRITICAL"), _("Could not locate a running Nagios process!"));
	}

	if (latest_entry_time == 0L) {
		die (STATE_CRITICAL, "NAGIOS %s: %s\n", _("CRITICAL"), _("Cannot parse Nagios log file for valid time"));
	}

	time (&current_time);
	if ((int)(current_time - latest_entry_time) > (expire_minutes * 60)) {
		result = STATE_WARNING;
	} else {
		result = STATE_OK;
	}

	printf ("NAGIOS %s: ", (result == STATE_OK) ? _("OK") : _("WARNING"));
	printf (ngettext ("%d process", "%d processes", proc_entries), proc_entries);
	printf (", ");
	printf (
	  ngettext ("status log updated %d second ago",
	    "status log updated %d seconds ago",
	    (int) (current_time - latest_entry_time) ),
	    (int) (current_time - latest_entry_time) );
	printf ("\n");

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"filename", required_argument, 0, 'F'},
		{"expires", required_argument, 0, 'e'},
		{"command", required_argument, 0, 'C'},
		{"timeout", optional_argument, 0, 't'},
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
			die (STATE_UNKNOWN,
								 _("Expiration time must be an integer (seconds)\n"));
		process_string = argv[3];
		return OK;
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVvF:C:e:t:", longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
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
				die (STATE_UNKNOWN,
				     _("Expiration time must be an integer (seconds)\n"));
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg))
				timeout_interval = atoi (optarg);
			else
				die (STATE_UNKNOWN,
				     _("Timeout must be an integer (seconds)\n"));
			break;
		case 'v':
			verbose++;
			break;
		default:									/* print short usage_va statement if args not parsable */
			usage5();
		}
	}


	if (status_log == NULL)
		die (STATE_UNKNOWN, _("You must provide the status_log\n"));

	if (process_string == NULL)
		die (STATE_UNKNOWN, _("You must provide a process string\n"));

	return OK;
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (_(COPYRIGHT), copyright, email);

	printf ("%s\n", _("This plugin checks the status of the Nagios process on the local machine"));
  printf ("%s\n", _("The plugin will check to make sure the Nagios status log is no older than"));
  printf ("%s\n", _("the number of minutes specified by the expires option."));
  printf ("%s\n", _("It also checks the process table for a process matching the command argument."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-F, --filename=FILE");
  printf ("    %s\n", _("Name of the log file to check"));
  printf (" %s\n", "-e, --expires=INTEGER");
  printf ("    %s\n", _("Minutes aging after which logfile is considered stale"));
  printf (" %s\n", "-C, --command=STRING");
  printf ("    %s\n", _("Substring to search for in process arguments"));
  printf (" %s\n", "-t, --timeout=INTEGER");
  printf ("    %s\n", _("Timeout for the plugin in seconds"));
  printf (UT_VERBOSE);

  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_nagios -t 20 -e 5 -F /usr/local/nagios/var/status.log -C /usr/local/nagios/bin/nagios");

  printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -F <status log file> -t <timeout_seconds> -e <expire_minutes> -C <process_string>\n", progname);
}
