/******************************************************************************
 *
 * Program: SNMP plugin for Nagios
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
 *****************************************************************************/

#include "config.h"
#include "common.h"
#include "utils.h"
#include "popen.h"

const char *progname = "check_dig";
#define REVISION "$Revision$"
#define COPYRIGHT "2000-2002"
#define AUTHOR "Karl DeBisschop"
#define EMAIL "karl@debisschop.net"
#define SUMMARY "Test the DNS service on the specified host using dig\n"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char *query_address = NULL;
char *dns_server = NULL;
int verbose = FALSE;

int
main (int argc, char **argv)
{
	char input_buffer[MAX_INPUT_BUFFER];
	char *command_line = NULL;
	char *output = "";
	int result = STATE_UNKNOWN;

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR)
		usage ("Cannot catch SIGALRM\n");

	if (process_arguments (argc, argv) != OK)
		usage ("Could not parse arguments\n");

	/* get the command to run */
	asprintf (&command_line, "%s @%s %s", PATH_TO_DIG, dns_server, query_address);

	alarm (timeout_interval);
	time (&start_time);

	if (verbose)
		printf ("%s\n", command_line);
	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", command_line);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* the server is responding, we just got the host name... */
		if (strstr (input_buffer, ";; ANSWER SECTION:")) {

			/* get the host address */
			if (!fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
				break;

			if (strpbrk (input_buffer, "\r\n"))
				input_buffer[strcspn (input_buffer, "\r\n")] = '\0';

			if (strstr (input_buffer, query_address) == input_buffer) {
				asprintf (&output, input_buffer);
				result = STATE_OK;
			}
			else {
				asprintf (&output, "Server not found in ANSWER SECTION");
				result = STATE_WARNING;
			}

			continue;
		}

	}

	if (result != STATE_OK) {
		asprintf (&output, "No ANSWER SECTION found");
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		/* If we get anything on STDERR, at least set warning */
		result = max_state (result, STATE_WARNING);
		printf ("%s", input_buffer);
		if (strlen (output) == 0)
			asprintf (&output, 1 + index (input_buffer, ':'));
	}

	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		result = max_state (result, STATE_WARNING);
		if (strlen (output) == 0)
			asprintf (&output, "dig returned error status");
	}

	(void) time (&end_time);

	if (output == NULL || strlen (output) == 0)
		asprintf (&output, " Probably a non-existent host/domain");

	if (result == STATE_OK)
		printf ("DNS OK - %d seconds response time (%s)\n",
						(int) (end_time - start_time), output);
	else if (result == STATE_WARNING)
		printf ("DNS WARNING - %s\n", output);
	else if (result == STATE_CRITICAL)
		printf ("DNS CRITICAL - %s\n", output);
	else
		printf ("DNS problem - %s\n", output);

	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
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
		c = getopt_long (argc, argv, "hVvt:l:H:", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage3 ("Unknown argument", optopt);
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				dns_server = optarg;
			}
			else {
				usage ("Invalid host name\n");
			}
			break;
		case 'l':									/* username */
			query_address = optarg;
			break;
		case 'v':									/* verbose */
			verbose = TRUE;
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				timeout_interval = atoi (optarg);
			}
			else {
				usage ("Time interval must be a nonnegative integer\n");
			}
			break;
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		}
	}

	c = optind;
	if (dns_server == NULL) {
		if (c < argc) {
			if (is_host (argv[c])) {
				dns_server = argv[c];
			}
			else {
				usage ("Invalid host name");
			}
		}
		else {
			asprintf (&dns_server, "127.0.0.1");
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
	print_revision (progname, "$Revision$");
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n"
		 " -H, --hostname=STRING or IPADDRESS\n"
		 "   Check server on the indicated host\n"
		 " -l, --lookup=STRING\n"
		 "   machine name to lookup\n"
		 " -t, --timeout=INTEGER\n"
		 "   Seconds before connection attempt times out (default: %d)\n"
		 " -v, --verbose\n"
		 "   Print extra information (command-line use only)\n"
		 " -h, --help\n"
		 "   Print detailed help screen\n"
		 " -V, --version\n"
		 "   Print version information\n\n", DEFAULT_SOCKET_TIMEOUT);
	support ();
}





void
print_usage (void)
{
	printf
		("Usage: %s -H host -l lookup [-t timeout] [-v]\n"
		 "       %s --help\n"
		 "       %s --version\n", progname, progname, progname);
}
