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

*****************************************************************************/

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "popen.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

const char *progname = "check_dig";
const char *revision = "$Revision$";
const char *copyright = "2002-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

enum {
	DEFAULT_PORT = 53
};

char *query_address = NULL;
char *dns_server = NULL;
int verbose = FALSE;
int server_port = DEFAULT_PORT;
int warning_interval = -1;
int critical_interval = -1;

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
		usage (_("Cannot catch SIGALRM\n"));

	if (process_arguments (argc, argv) != OK)
		usage (_("Could not parse arguments\n"));

	/* get the command to run */
	asprintf (&command_line, "%s @%s -p %d %s",
	          PATH_TO_DIG, dns_server, server_port, query_address);

	alarm (timeout_interval);
	gettimeofday (&tv, NULL);

	if (verbose)
		printf ("%s\n", command_line);

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

			/* get the host address */
			if (!fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
				break;

			if (strpbrk (input_buffer, "\r\n"))
				input_buffer[strcspn (input_buffer, "\r\n")] = '\0';

			if (strstr (input_buffer, query_address) == input_buffer) {
				output = strdup(input_buffer);
				result = STATE_OK;
			}
			else {
				asprintf (&output, _("Server not found in ANSWER SECTION"));
				result = STATE_WARNING;
			}

			continue;
		}

	}

	if (result != STATE_OK) {
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
			asprintf (&output, _("dig returned error status"));
	}

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (output == NULL || strlen (output) == 0)
		asprintf (&output, _(" Probably a non-existent host/domain"));

	if (elapsed_time > critical_interval)
		die (STATE_CRITICAL,
		     _("DNS OK - %d seconds response time (%s)|time=%ldus\n"),
		     elapsed_time, output, microsec);

	else if (result == STATE_CRITICAL)
		printf (_("DNS CRITICAL - %s|time=%ldus\n"), output);

	else if (elapsed_time > warning_interval)
		die (STATE_WARNING,
		     _("DNS OK - %d seconds response time (%s)|time=%ldus\n"),
		     elapsed_time, output, microsec);

	else if (result == STATE_WARNING)
		printf (_("DNS WARNING - %s|time=%ldus\n"), output);

	else if (result == STATE_OK)
		printf (_("DNS OK - %d seconds response time (%s)|time=%ldus\n"),
						elapsed_time, output, microsec);

	else
		printf (_("DNS problem - %s|time=%ldus\n"), output);

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
		{"query_address", required_argument, 0, 'e'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "hVvt:l:H:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage3 (_("Unknown argument"), optopt);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				dns_server = optarg;
			}
			else {
				usage2 (_("Invalid host name"), optarg);
			}
			break;
		case 'p':
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage2 (_("Server port must be a nonnegative integer\n"), optarg);
			}
			break;
		case 'l':									/* username */
			query_address = optarg;
			break;
		case 'w':									/* timeout */
			if (is_intnonneg (optarg)) {
				warning_interval = atoi (optarg);
			}
			else {
				usage2 (_("Warning interval must be a nonnegative integer\n"), optarg);
			}
			break;
		case 'c':									/* timeout */
			if (is_intnonneg (optarg)) {
				critical_interval = atoi (optarg);
			}
			else {
				usage2 (_("Critical interval must be a nonnegative integer\n"), optarg);
			}
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				timeout_interval = atoi (optarg);
			}
			else {
				usage2 (_("Time interval must be a nonnegative integer\n"), optarg);
			}
			break;
		case 'v':									/* verbose */
			verbose = TRUE;
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
				usage2 (_("Invalid host name"), argv[c]);
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

	printf (_("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("Test the DNS service on the specified host using dig\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'P', myport);

	printf (_("\
 -l, --lookup=STRING\n\
   machine name to lookup\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf (_(UT_SUPPORT));
}




void
print_usage (void)
{
	printf (_("\
Usage: %s -H host -l lookup [-p <server port>] [-w <warning interval>]\n\
         [-c <critical interval>] [-t <timeout>] [-v]\n"),
	        progname);
	printf ("       %s (-h|--help)\n", progname);
	printf ("       %s (-V|--version)\n", progname);
}
