/******************************************************************************
 *
 * CHECK_DNS.C
 *
 * Program: DNS plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * Last Modified: $Date$
 *
 * Notes:
 *  - Safe popen added by Karl DeBisschop 9-11-99
 *  - expected-address parameter added by Alex Chaffee - 7 Oct 2002
 *
 * Command line: (see print_usage)
 *
 * Description:
 *
 * This program will use the nslookup program to obtain the IP address
 * for a given host name.  A optional DNS server may be specified.  If
 * no DNS server is specified, the default server(s) for the system
 * are used.
 *
 * Return Values:
 *  OK           The DNS query was successful (host IP address was returned).
 *  WARNING      The DNS server responded, but could not fulfill the request.
 *  CRITICAL     The DNS server is not responding or encountered an error.
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

const char *progname = "check_dns";
#define REVISION "$Revision$"
#define COPYRIGHT "2000-2002"

int process_arguments (int, char **);
int validate_arguments (void);
void print_usage (void);
void print_help (void);
int error_scan (char *);

#define ADDRESS_LENGTH 256
char query_address[ADDRESS_LENGTH] = "";
char dns_server[ADDRESS_LENGTH] = "";
char ptr_server[ADDRESS_LENGTH] = "";
int verbose = FALSE;
char expected_address[ADDRESS_LENGTH] = "";
int match_expected_address = FALSE;

int
main (int argc, char **argv)
{
	char *command_line = NULL;
	char input_buffer[MAX_INPUT_BUFFER];
	char *output = NULL;
	char *address = NULL;
	char *temp_buffer = NULL;
	int result = STATE_UNKNOWN;
	double elapsed_time;
	struct timeval tv;
	int multi_address;

	/* Set signal handling and alarm */
	if (signal (SIGALRM, popen_timeout_alarm_handler) == SIG_ERR) {
		printf ("Cannot catch SIGALRM");
		return STATE_UNKNOWN;
	}

	if (process_arguments (argc, argv) != OK) {
		print_usage ();
		return STATE_UNKNOWN;
	}

	/* get the command to run */
	asprintf (&command_line, "%s %s %s", NSLOOKUP_COMMAND,	query_address, dns_server);

	alarm (timeout_interval);
	gettimeofday (&tv, NULL);

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

	/* scan stdout */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		if (verbose)
			printf ("%s\n", input_buffer);

		if (strstr (input_buffer, ".in-addr.arpa")) {
			if ((temp_buffer = strstr (input_buffer, "name = ")))
				address = strscpy (address, temp_buffer + 7);
			else {
				output = strscpy (output, "Unknown error (plugin)");
				result = STATE_WARNING;
			}
		}

		/* the server is responding, we just got the host name... */
		if (strstr (input_buffer, "Name:")) {

			/* get the host address */
			if (!fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process))
				break;

			if (verbose)
				printf ("%s\n", input_buffer);

			if ((temp_buffer = index (input_buffer, ':'))) {
				temp_buffer++;
				/* Strip leading spaces */
				for (; *temp_buffer != '\0' && *temp_buffer == ' '; temp_buffer++)
					/* NOOP */;
				address = strscpy (address, temp_buffer);
				strip (address);
				result = STATE_OK;
			}
			else {
				output = strscpy (output, "Unknown error (plugin)");
				result = STATE_WARNING;
			}

			break;
		}

		result = error_scan (input_buffer);
		if (result != STATE_OK) {
			output = strscpy (output, 1 + index (input_buffer, ':'));
			strip (output);
			break;
		}

	}

	/* scan stderr */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (error_scan (input_buffer) != STATE_OK) {
			result = max_state (result, error_scan (input_buffer));
			output = strscpy (output, 1 + index (input_buffer, ':'));
			strip (output);
		}
	}

	/* close stderr */
	(void) fclose (child_stderr);

	/* close stdout */
	if (spclose (child_process)) {
		result = max_state (result, STATE_WARNING);
		if (!strcmp (output, ""))
			output = strscpy (output, "nslookup returned error status");
	}

	/* compare to expected address */
	if (result == STATE_OK && match_expected_address && strcmp(address, expected_address)) {
	        result = STATE_CRITICAL;
	        asprintf(&output, "expected %s but got %s", expected_address, address);
		}
	
	elapsed_time = delta_time (tv);

	if (result == STATE_OK) {
		if (strchr (address, ',') == NULL)
			multi_address = FALSE;
		else
			multi_address = TRUE;

		printf ("DNS ok - %-5.3f seconds response time, address%s %s|time=%-5.3f\n",
		  elapsed_time, (multi_address==TRUE ? "es are" : " is"), address, elapsed_time);
	}
	else if (result == STATE_WARNING)
		printf ("DNS WARNING - %s\n",
			!strcmp (output, "") ? " Probably a non-existent host/domain" : output);
	else if (result == STATE_CRITICAL)
		printf ("DNS CRITICAL - %s\n",
			!strcmp (output, "") ? " Probably a non-existent host/domain" : output);
	else
		printf ("DNS problem - %s\n",
			!strcmp (output, "") ? " Probably a non-existent host/domain" : output);

	return result;
}

int
error_scan (char *input_buffer)
{

	/* the DNS lookup timed out */
	if (strstr (input_buffer,
	     "Note:  nslookup is deprecated and may be removed from future releases.")
	    || strstr (input_buffer,
	     "Consider using the `dig' or `host' programs instead.  Run nslookup with")
	    || strstr (input_buffer,
	     "the `-sil[ent]' option to prevent this message from appearing."))
		return STATE_OK;

	/* the DNS lookup timed out */
	else if (strstr (input_buffer, "Timed out"))
		return STATE_WARNING;

	/* DNS server is not running... */
	else if (strstr (input_buffer, "No response from server"))
		return STATE_CRITICAL;

	/* Host name is valid, but server doesn't have records... */
	else if (strstr (input_buffer, "No records"))
		return STATE_WARNING;

	/* Host or domain name does not exist */
	else if (strstr (input_buffer, "Non-existent"))
		return STATE_CRITICAL;
	else if (strstr (input_buffer, "** server can't find"))
		return STATE_CRITICAL;
	else if(strstr(input_buffer,"NXDOMAIN")) /* 9.x */
		return STATE_CRITICAL;

	/* Connection was refused */
	else if (strstr (input_buffer, "Connection refused"))
		return STATE_CRITICAL;

	/* Network is unreachable */
	else if (strstr (input_buffer, "Network is unreachable"))
		return STATE_CRITICAL;

	/* Internal server failure */
	else if (strstr (input_buffer, "Server failure"))
		return STATE_CRITICAL;

	/* DNS server refused to service request */
	else if (strstr (input_buffer, "Refused"))
		return STATE_CRITICAL;

	/* Request error */
	else if (strstr (input_buffer, "Format error"))
		return STATE_WARNING;

	else
		return STATE_OK;

}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int opt_index = 0;
	static struct option long_opts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"server", required_argument, 0, 's'},
		{"reverse-server", required_argument, 0, 'r'},
		{"expected-address", required_argument, 0, 'a'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "hVvt:H:s:r:a:", long_opts, &opt_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?': /* args not parsable */
			printf ("%s: Unknown argument: %s\n\n", progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h': /* help */
			print_help ();
			exit (STATE_OK);
		case 'V': /* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 'v': /* version */
			verbose = TRUE;
			break;
		case 't': /* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'H': /* hostname */
			if (strlen (optarg) >= ADDRESS_LENGTH)
				terminate (STATE_UNKNOWN, "Input buffer overflow\n");
			strcpy (query_address, optarg);
			break;
		case 's': /* server name */
			/* TODO: this is_host check is probably unnecessary. Better to confirm nslookup
			   response matches */
			if (is_host (optarg) == FALSE) {
				printf ("Invalid server name/address\n\n");
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			if (strlen (optarg) >= ADDRESS_LENGTH)
				terminate (STATE_UNKNOWN, "Input buffer overflow\n");
			strcpy (dns_server, optarg);
			break;
		case 'r': /* reverse server name */
			/* TODO: Is this is_host necessary? */
			if (is_host (optarg) == FALSE) {
				printf ("Invalid host name/address\n\n");
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			if (strlen (optarg) >= ADDRESS_LENGTH)
				terminate (STATE_UNKNOWN, "Input buffer overflow\n");
			strcpy (ptr_server, optarg);
			break;
		case 'a': /* expected address */
			if (strlen (optarg) >= ADDRESS_LENGTH)
				terminate (STATE_UNKNOWN, "Input buffer overflow\n");
			strcpy (expected_address, optarg);
			match_expected_address = TRUE;
			break;
		}
	}

	c = optind;
	if (strlen(query_address)==0 && c<argc) {
		if (strlen(argv[c])>=ADDRESS_LENGTH)
			terminate (STATE_UNKNOWN, "Input buffer overflow\n");
		strcpy (query_address, argv[c++]);
	}

	if (strlen(dns_server)==0 && c<argc) {
		/* TODO: See -s option */
		if (is_host(argv[c]) == FALSE) {
			printf ("Invalid name/address: %s\n\n", argv[c]);
			return ERROR;
		}
		if (strlen(argv[c]) >= ADDRESS_LENGTH)
			terminate (STATE_UNKNOWN, "Input buffer overflow\n");
		strcpy (dns_server, argv[c++]);
	}

	return validate_arguments ();
}

int
validate_arguments ()
{
	if (query_address[0] == 0)
		return ERROR;
	else
		return OK;
}

void
print_usage (void)
{
	printf ("Usage: %s -H host [-s server] [-a expected-address] [-t timeout]\n" "       %s --help\n"
					"       %s --version\n", progname, progname, progname);
}

void
print_help (void)
{
	print_revision (progname, REVISION);
	printf ("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 "-H, --hostname=HOST\n"
		 "   The name or address you want to query\n"
		 "-s, --server=HOST\n"
		 "   Optional DNS server you want to use for the lookup\n"
		 "-a, --expected-address=IP-ADDRESS\n"
		 "   Optional IP address you expect the DNS server to return\n"
		 "-t, --timeout=INTEGER\n"
		 "   Seconds before connection times out (default: %d)\n"
		 "-h, --help\n"
		 "   Print detailed help\n"
		 "-V, --version\n"
		 "   Print version numbers and license information\n"
		 "\n"
		 "This plugin uses the nslookup program to obtain the IP address\n"
		 "for the given host/domain query.  A optional DNS server to use may\n"
		 "be specified.  If no DNS server is specified, the default server(s)\n"
		 "specified in /etc/resolv.conf will be used.\n", DEFAULT_SOCKET_TIMEOUT);
}
