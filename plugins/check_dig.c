/*****************************************************************************

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

 $Id$
 
*****************************************************************************/

const char *progname = "check_dig";
const char *revision = "$Revision$";
const char *copyright = "2002-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "popen.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

enum {
	UNDEFINED = 0,
	DEFAULT_PORT = 53
};

char *query_address = NULL;
char *record_type = "A";
char *expected_address = NULL;
char *dns_server = NULL;
int verbose = FALSE;
int server_port = DEFAULT_PORT;
double warning_interval = UNDEFINED;
double critical_interval = UNDEFINED;
struct timeval tv;

int
main (int argc, char **argv)
{
	char input_buffer[MAX_INPUT_BUFFER];
	char *command_line;
	char *output;
	long microsec;
	double elapsed_time;
	int result = STATE_UNKNOWN;

	output = strdup ("");

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR)
		usage4 (_("Cannot catch SIGALRM"));

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* get the command to run */
	asprintf (&command_line, "%s @%s -p %d %s -t %s",
	          PATH_TO_DIG, dns_server, server_port, query_address, record_type);

	alarm (timeout_interval);
	gettimeofday (&tv, NULL);

	if (verbose) {
		printf ("%s\n", command_line);
		if(expected_address != NULL) {
			printf ("Looking for: '%s'\n", expected_address);
		} else {
			printf ("Looking for: '%s'\n", query_address);
		}
	}

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), command_line);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* the server is responding, we just got the host name... */
		if (strstr (input_buffer, ";; ANSWER SECTION:")) {

			/* loop through the whole 'ANSWER SECTION' */
			do {
				/* get the host address */
				if (!fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
					break;

				if (strpbrk (input_buffer, "\r\n"))
					input_buffer[strcspn (input_buffer, "\r\n")] = '\0';

				if (verbose && !strstr (input_buffer, ";; ")) 
					printf ("%s\n", input_buffer); 

				if (expected_address==NULL && strstr (input_buffer, query_address) != NULL) {
					output = strdup(input_buffer);
					result = STATE_OK;
				}
				else if (expected_address != NULL && strstr (input_buffer, expected_address) != NULL) {
					output = strdup(input_buffer);
                        	        result = STATE_OK;
				}

			} while (!strstr (input_buffer, ";; "));

			if (result == STATE_UNKNOWN) {
		        	asprintf (&output, _("Server not found in ANSWER SECTION"));
	                        result = STATE_WARNING;
                        }
		}

	}

	if (result == STATE_UNKNOWN) {
		asprintf (&output, _("No ANSWER SECTION found"));
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		/* If we get anything on STDERR, at least set warning */
		result = max_state (result, STATE_WARNING);
		printf ("%s", input_buffer);
		if (strlen (output) == 0)
			output = strdup (1 + index (input_buffer, ':'));
	}

	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		result = max_state (result, STATE_WARNING);
		if (strlen (output) == 0)
			asprintf (&output, _("dig returned an error status"));
	}

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (output == NULL || strlen (output) == 0)
		asprintf (&output, _(" Probably a non-existent host/domain"));

	if (critical_interval > UNDEFINED && elapsed_time > critical_interval)
		result = STATE_CRITICAL;

	else if (warning_interval > UNDEFINED && elapsed_time > warning_interval)
		result = STATE_WARNING;

	asprintf (&output, _("%.3f seconds response time (%s)"), elapsed_time, output);

	printf ("DNS %s - %s|%s\n",
	        state_text (result), output,
	        fperfdata("time", elapsed_time, "s",
	                 (warning_interval>UNDEFINED?TRUE:FALSE),
	                 warning_interval,
	                 (critical_interval>UNDEFINED?TRUE:FALSE),
	                 critical_interval,
									 TRUE, 0, FALSE, 0));
	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"query_address", required_argument, 0, 'l'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"record_type", required_argument, 0, 'T'},
		{"expected_address", required_argument, 0, 'a'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hVvt:l:H:w:c:T:a:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				dns_server = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'p':                 /* server port */
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage2 (_("Port must be a positive integer"), optarg);
			}
			break;
		case 'l':									/* address to lookup */
			query_address = optarg;
			break;
		case 'w':									/* warning */
			if (is_nonnegative (optarg)) {
				warning_interval = strtod (optarg, NULL);
			}
			else {
				usage2 (_("Warning interval must be a positive integer"), optarg);
			}
			break;
		case 'c':									/* critical */
			if (is_nonnegative (optarg)) {
				critical_interval = strtod (optarg, NULL);
			}
			else {
				usage2 (_("Critical interval must be a positive integer"), optarg);
			}
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				timeout_interval = atoi (optarg);
			}
			else {
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			}
			break;
		case 'v':									/* verbose */
			verbose = TRUE;
			break;
		case 'T':
			record_type = optarg;
			break;
		case 'a':
			expected_address = optarg;
			break;
		}
	}

	c = optind;
	if (dns_server == NULL) {
		if (c < argc) {
			if (is_host (argv[c])) {
				dns_server = argv[c];
			}
			else {
				usage2 (_("Invalid hostname/address"), argv[c]);
			}
		}
		else {
			dns_server = strdup ("127.0.0.1");
		}
	}

	return validate_arguments ();
}



int
validate_arguments (void)
{
	return OK;
}



void
print_help (void)
{
	char *myport;

	asprintf (&myport, "%d", DEFAULT_PORT);

	print_revision (progname, revision);

	printf ("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("Test the DNS service on the specified host using dig\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'P', myport);

	printf (_("\
 -l, --lookup=STRING\n\
   machine name to lookup\n"));

        printf (_("\
 -T, --record_type=STRING\n\
   record type to lookup (default: A)\n"));

        printf (_("\
 -a, --expected_address=STRING\n\
   an address expected to be in the asnwer section.\n\
   if not set, uses whatever was in -l\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s -H host -l lookup [-p <server port>] [-T <query type>]\n\
         [-w <warning interval>] [-c <critical interval>] [-t <timeout>]\n\
         [-a <expected answer address>] [-v]\n", progname);
}
