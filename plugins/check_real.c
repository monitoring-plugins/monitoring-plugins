/*****************************************************************************
*
* CHECK_REAL.C
*
* Program: RealMedia plugin for Nagios
* License: GPL
* Copyright (c) 1999 Pedro Leite (leite@cic.ua.pt)
*
* Based on CHECK_HTTP.C
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Command line: CHECK_REAL <host_address> [-e expect] [-u url] [-p port]
*                          [-hn host_name] [-wt warn_time] [-ct crit_time]
*                          [-to to_sec]
*
* Description:
*
* This plugin will attempt to open an RTSP connection with the host.
* Successul connects return STATE_OK, refusals and timeouts return
* STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful connects,
* but incorrect reponse messages from the host result in STATE_WARNING return
* values.  If you are checking a virtual server that uses "host headers"you
* must supply the FQDN (fully qualified domain name) as the [host_name]
* argument.
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
****************************************************************************/

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

const char *progname = "check_real";

#define PORT	554
#define EXPECT	"RTSP/1."
#define URL	""

int process_arguments (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
void print_help (void);
void print_usage (void);

int server_port = PORT;
char *server_address = "";
char *host_name = "";
char *server_url = NULL;
char *server_expect = EXPECT;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
int verbose = FALSE;

int
main (int argc, char **argv)
{
	int sd;
	int result;
	char buffer[MAX_INPUT_BUFFER];
	char *status_line = NULL;

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);
	time (&start_time);

	/* try to connect to the host at the given port number */
	if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
		terminate (STATE_CRITICAL, "Unable to connect to %s on port %d\n",
							 server_address, server_port);

	/* Part I - Server Check */

	/* send the OPTIONS request */
	sprintf (buffer, "OPTIONS rtsp://%s:%d RTSP/1.0\r\n", host_name, server_port);
	result = send (sd, buffer, strlen (buffer), 0);

	/* send the header sync */
	sprintf (buffer, "CSeq: 1\r\n");
	result = send (sd, buffer, strlen (buffer), 0);

	/* send a newline so the server knows we're done with the request */
	sprintf (buffer, "\r\n");
	result = send (sd, buffer, strlen (buffer), 0);

	/* watch for the REAL connection string */
	result = recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0);

	/* return a CRITICAL status if we couldn't read any data */
	if (result == -1)
		terminate (STATE_CRITICAL, "No data received from %s\n", host_name);

	/* make sure we find the response we are looking for */
	if (!strstr (buffer, server_expect)) {
		if (server_port == PORT)
			printf ("Invalid REAL response received from host\n");
		else
			printf ("Invalid REAL response received from host on port %d\n",
							server_port);
	}
	else {
		/* else we got the REAL string, so check the return code */

		time (&end_time);

		result = STATE_OK;

		status_line = (char *) strtok (buffer, "\n");

		if (strstr (status_line, "200"))
			result = STATE_OK;

		/* client errors result in a warning state */
		else if (strstr (status_line, "400"))
			result = STATE_WARNING;
		else if (strstr (status_line, "401"))
			result = STATE_WARNING;
		else if (strstr (status_line, "402"))
			result = STATE_WARNING;
		else if (strstr (status_line, "403"))
			result = STATE_WARNING;
		else if (strstr (status_line, "404"))
			result = STATE_WARNING;

		/* server errors result in a critical state */
		else if (strstr (status_line, "500"))
			result = STATE_CRITICAL;
		else if (strstr (status_line, "501"))
			result = STATE_CRITICAL;
		else if (strstr (status_line, "502"))
			result = STATE_CRITICAL;
		else if (strstr (status_line, "503"))
			result = STATE_CRITICAL;

		else
			result = STATE_UNKNOWN;
	}

	/* Part II - Check stream exists and is ok */
	if ((result == STATE_OK )&& (server_url != NULL) ) {

		/* Part I - Server Check */

		/* send the OPTIONS request */
		sprintf (buffer, "DESCRIBE rtsp://%s:%d%s RTSP/1.0\n", host_name,
						 server_port, server_url);
		result = send (sd, buffer, strlen (buffer), 0);

		/* send the header sync */
		sprintf (buffer, "CSeq: 2\n");
		result = send (sd, buffer, strlen (buffer), 0);

		/* send a newline so the server knows we're done with the request */
		sprintf (buffer, "\n");
		result = send (sd, buffer, strlen (buffer), 0);

		/* watch for the REAL connection string */
		result = recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0);

		/* return a CRITICAL status if we couldn't read any data */
		if (result == -1) {
			printf ("No data received from host\n");
			result = STATE_CRITICAL;
		}
		else {
			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {
				if (server_port == PORT)
					printf ("Invalid REAL response received from host\n");
				else
					printf ("Invalid REAL response received from host on port %d\n",
									server_port);
			}
			else {

				/* else we got the REAL string, so check the return code */

				time (&end_time);

				result = STATE_OK;

				status_line = (char *) strtok (buffer, "\n");

				if (strstr (status_line, "200"))
					result = STATE_OK;

				/* client errors result in a warning state */
				else if (strstr (status_line, "400"))
					result = STATE_WARNING;
				else if (strstr (status_line, "401"))
					result = STATE_WARNING;
				else if (strstr (status_line, "402"))
					result = STATE_WARNING;
				else if (strstr (status_line, "403"))
					result = STATE_WARNING;
				else if (strstr (status_line, "404"))
					result = STATE_WARNING;

				/* server errors result in a critical state */
				else if (strstr (status_line, "500"))
					result = STATE_CRITICAL;
				else if (strstr (status_line, "501"))
					result = STATE_CRITICAL;
				else if (strstr (status_line, "502"))
					result = STATE_CRITICAL;
				else if (strstr (status_line, "503"))
					result = STATE_CRITICAL;

				else
					result = STATE_UNKNOWN;
			}
		}
	}

	/* Return results */
	if (result == STATE_OK) {

		if (check_critical_time == TRUE
				&& (end_time - start_time) > critical_time) result = STATE_CRITICAL;
		else if (check_warning_time == TRUE
						 && (end_time - start_time) > warning_time) result =
				STATE_WARNING;

		/* Put some HTML in here to create a dynamic link */
		printf ("REAL %s - %d second response time\n",
						(result == STATE_OK) ? "ok" : "problem",
						(int) (end_time - start_time));
	}
	else
		printf ("%s\n", status_line);

	/* close the connection */
	close (sd);

	/* reset the alarm */
	alarm (0);

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
		{"IPaddress", required_argument, 0, 'I'},
		{"expect", required_argument, 0, 'e'},
		{"url", required_argument, 0, 'u'},
		{"port", required_argument, 0, 'p'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVI:H:e:u:p:w:c:t:", long_options,
									 &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'I':									/* hostname */
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage ("Invalid host name\n");
			}
			break;
		case 'e':									/* string to expect in response header */
			server_expect = optarg;
			break;
		case 'u':									/* server URL */
			server_url = optarg;
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage ("Server port must be a positive integer\n");
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_time = atoi (optarg);
				check_warning_time = TRUE;
			}
			else {
				usage ("Warning time must be a nonnegative integer\n");
			}
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_time = atoi (optarg);
				check_critical_time = TRUE;
			}
			else {
				usage ("Critical time must be a nonnegative integer\n");
			}
			break;
		case 'v':									/* verbose */
			verbose = TRUE;
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				socket_timeout = atoi (optarg);
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
		case '?':									/* help */
			usage ("Invalid argument\n");
		}
	}

	c = optind;
	if (strlen(server_address)==0 && argc>c) {
		if (is_host (argv[c])) {
			server_address = argv[c++];
		}
		else {
			usage ("Invalid host name");
		}
	}

	if (strlen(server_address) == 0)
		usage ("You must provide a server to check\n");

	if (strlen(host_name) == 0)
		asprintf (&host_name, "%s", server_address);

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
		("Copyright (c) 2000 Pedro Leite (leite@cic.ua.pt)/Karl DeBisschop\n\n"
		 "This plugin tests the REAL service on the specified host.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -H, --hostname=STRING or IPADDRESS\n"
		 "   Check this server on the indicated host\n"
		 " -I, --IPaddress=STRING or IPADDRESS\n"
		 "   Check server at this host address\n"
		 " -p, --port=INTEGER\n"
		 "   Make connection on the indicated port (default: %d)\n"
		 " -u, --url=STRING\n"
		 "   Connect to this url\n"
		 " -e, --expect=STRING\n"
		 "   String to expect in first line of server response (default: %s)\n"
		 " -w, --warning=INTEGER\n"
		 "   Seconds necessary to result in a warning status\n"
		 " -c, --critical=INTEGER\n"
		 "   Seconds necessary to result in a critical status\n"
		 " -t, --timeout=INTEGER\n"
		 "   Seconds before connection attempt times out (default: %d)\n"
		 " -v, --verbose\n"
		 "   Print extra information (command-line use only)\n"
		 " -h, --help\n"
		 "   Print detailed help screen\n"
		 " -V, --version\n"
		 "   Print version information\n\n",
		 PORT, EXPECT, DEFAULT_SOCKET_TIMEOUT);
	support ();
}





void
print_usage (void)
{
	printf
		("Usage: %s -H host [-e expect] [-p port] [-w warn] [-c crit]\n"
		 "            [-t timeout] [-v]\n"
		 "       %s --help\n"
		 "       %s --version\n", progname, progname, progname);
}
