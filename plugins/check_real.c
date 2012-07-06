/*****************************************************************************
* 
* Nagios check_real plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_real plugin
* 
* This plugin tests the REAL service on the specified host.
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

const char *progname = "check_real";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum {
	PORT	= 554
};

#define EXPECT	"RTSP/1."
#define URL	""

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int server_port = PORT;
char *server_address;
char *host_name;
char *server_url = NULL;
char *server_expect;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
int verbose = FALSE;



int
main (int argc, char **argv)
{
	int sd;
	int result = STATE_UNKNOWN;
	char buffer[MAX_INPUT_BUFFER];
	char *status_line = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);
	time (&start_time);

	/* try to connect to the host at the given port number */
	if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
		die (STATE_CRITICAL, _("Unable to connect to %s on port %d\n"),
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
		die (STATE_CRITICAL, _("No data received from %s\n"), host_name);

	/* make sure we find the response we are looking for */
	if (!strstr (buffer, server_expect)) {
		if (server_port == PORT)
			printf ("%s\n", _("Invalid REAL response received from host"));
		else
			printf (_("Invalid REAL response received from host on port %d\n"),
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
			printf (_("No data received from host\n"));
			result = STATE_CRITICAL;
		}
		else {
			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {
				if (server_port == PORT)
					printf ("%s\n", _("Invalid REAL response received from host"));
				else
					printf (_("Invalid REAL response received from host on port %d\n"),
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
		printf (_("REAL %s - %d second response time\n"),
						state_text (result),
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

	int option = 0;
	static struct option longopts[] = {
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
		c = getopt_long (argc, argv, "+hvVI:H:e:u:p:w:c:t:", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'I':									/* hostname */
		case 'H':									/* hostname */
			if (server_address)
				break;
			else if (is_host (optarg))
				server_address = optarg;
			else
				usage2 (_("Invalid hostname/address"), optarg);
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
				usage4 (_("Port must be a positive integer"));
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_time = atoi (optarg);
				check_warning_time = TRUE;
			}
			else {
				usage4 (_("Warning time must be a positive integer"));
			}
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_time = atoi (optarg);
				check_critical_time = TRUE;
			}
			else {
				usage4 (_("Critical time must be a positive integer"));
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
				usage4 (_("Timeout interval must be a positive integer"));
			}
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* usage */
			usage5 ();
		}
	}

	c = optind;
	if (server_address==NULL && argc>c) {
		if (is_host (argv[c])) {
			server_address = argv[c++];
		}
		else {
			usage2 (_("Invalid hostname/address"), argv[c]);
		}
	}

	if (server_address==NULL)
		usage4 (_("You must provide a server to check"));

	if (host_name==NULL)
		host_name = strdup (server_address);

	if (server_expect == NULL)
		server_expect = strdup(EXPECT);

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
	xasprintf (&myport, "%d", PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Pedro Leite <leite@cic.ua.pt>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin tests the REAL service on the specified host."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (" %s\n", "-u, --url=STRING");
  printf ("    %s\n", _("Connect to this url"));
  printf (" %s\n", "-e, --expect=STRING");
  printf (_("String to expect in first line of server response (default: %s)\n"),
	       EXPECT);

	printf (UT_WARN_CRIT);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

  printf ("\n");
	printf ("%s\n", _("This plugin will attempt to open an RTSP connection with the host."));
  printf ("%s\n", _("Successul connects return STATE_OK, refusals and timeouts return"));
  printf ("%s\n", _("STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful connects,"));
  printf ("%s\n", _("but incorrect reponse messages from the host result in STATE_WARNING return"));
  printf ("%s\n", _("values."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host [-e expect] [-p port] [-w warn] [-c crit] [-t timeout] [-v]\n", progname);
}
