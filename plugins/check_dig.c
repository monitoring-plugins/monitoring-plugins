/******************************************************************************
*
* check_dig.c
*
* Program: dig plugin for Nagios
* License: GPL
* Copyright (c) 2000
*
* $Id$
*
*****************************************************************************/

#include "config.h"
#include "common.h"
#include "utils.h"
#include "popen.h"

#define PROGNAME "check_dig"

int process_arguments (int, char **);
int call_getopt (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
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
	char *output = NULL;
	int result = STATE_UNKNOWN;

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR)
		usage ("Cannot catch SIGALRM\n");

	if (process_arguments (argc, argv) != OK)
		usage ("Could not parse arguments\n");

	/* get the command to run */
	command_line =
		ssprintf (command_line, "%s @%s %s", PATH_TO_DIG, dns_server,
							query_address);

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

	output = strscpy (output, "");

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* the server is responding, we just got the host name... */
		if (strstr (input_buffer, ";; ANSWER SECTION:")) {

			/* get the host address */
			if (!fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
				break;

			if (strpbrk (input_buffer, "\r\n"))
				input_buffer[strcspn (input_buffer, "\r\n")] = '\0';

			if (strstr (input_buffer, query_address) == input_buffer) {
				output = strscpy (output, input_buffer);
				result = STATE_OK;
			}
			else {
				strcpy (output, "Server not found in ANSWER SECTION");
				result = STATE_WARNING;
			}

			continue;
		}

	}

	if (result != STATE_OK) {
		strcpy (output, "No ANSWER SECTION found");
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		/* If we get anything on STDERR, at least set warning */
		result = max (result, STATE_WARNING);
		printf ("%s", input_buffer);
		if (!strcmp (output, ""))
			strcpy (output, 1 + index (input_buffer, ':'));
	}

	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		result = max (result, STATE_WARNING);
		if (!strcmp (output, ""))
			strcpy (output, "nslookup returned error status");
	}

	(void) time (&end_time);

	if (result == STATE_OK)
		printf ("DNS ok - %d seconds response time (%s)\n",
						(int) (end_time - start_time), output);
	else if (result == STATE_WARNING)
		printf ("DNS WARNING - %s\n",
						!strcmp (output,
										 "") ? " Probably a non-existent host/domain" : output);
	else if (result == STATE_CRITICAL)
		printf ("DNS CRITICAL - %s\n",
						!strcmp (output,
										 "") ? " Probably a non-existent host/domain" : output);
	else
		printf ("DNS problem - %s\n",
						!strcmp (output,
										 "") ? " Probably a non-existent host/domain" : output);

	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	if (argc < 2)
		return ERROR;


	c = 0;
	while ((c += (call_getopt (argc - c, &argv[c]))) < argc) {

		if (is_option (argv[c]))
			continue;

		if (dns_server == NULL) {
			if (is_host (argv[c])) {
				dns_server = argv[c];
			}
			else {
				usage ("Invalid host name");
			}
		}
	}

	if (dns_server == NULL)
		dns_server = strscpy (NULL, "127.0.0.1");

	return validate_arguments ();
}






int
call_getopt (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"query_address", required_argument, 0, 'e'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "+hVvt:l:H:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?hVvt:l:H:");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 't':
		case 'l':
		case 'H':
			i++;
		}

		switch (c) {
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
			print_revision (PROGNAME, "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("Invalid argument\n");
		}
	}
	return i;
}





int
validate_arguments (void)
{
	return OK;
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 2000 Karl DeBisschop\n\n"
		 "This plugin use dig to test the DNS service on the specified host.\n\n");
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
		 "       %s --version\n", PROGNAME, PROGNAME, PROGNAME);
}
