/******************************************************************************
*
* CHECK_UDP.C
*
* Program: UDP port plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Command line: CHECK_UDP <host_address> [-p port] [-s send] [-e expect] \
*			   [-wt warn_time] [-ct crit_time] [-to to_sec]
*
* Description:
*
* This plugin will attempt to connect to the specified port
* on the host.  Successul connects return STATE_OK, refusals
* and timeouts return STATE_CRITICAL, other errors return
* STATE_UNKNOWN.
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

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#define PROGNAME "check_udp"

int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

int verbose = FALSE;
int server_port = 0;
char *server_address = NULL;
char *server_expect = NULL;
char *server_send = "";

int
main (int argc, char **argv)
{
	int result;
	char recv_buffer[MAX_INPUT_BUFFER];

	if (process_arguments (argc, argv) == ERROR)
		usage ("\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	time (&start_time);
	result =
		process_udp_request (server_address, server_port, server_send,
												 recv_buffer, MAX_INPUT_BUFFER - 1);
	time (&end_time);

	if (result != STATE_OK) {
		printf ("No response from host on port %d\n", server_port);
		result = STATE_CRITICAL;
	}

	else {

		/* check to see if we got the response we wanted */
		if (server_expect) {
			if (!strstr (recv_buffer, server_expect)) {
				printf ("Invalid response received from host on port %d\n",
								server_port);
				result = STATE_CRITICAL;
			}
		}
	}

	/* we connected, so close connection before exiting */
	if (result == STATE_OK) {

		if (check_critical_time == TRUE
				&& (end_time - start_time) > critical_time) result = STATE_CRITICAL;
		else if (check_warning_time == TRUE
						 && (end_time - start_time) > warning_time) result =
				STATE_WARNING;

		printf ("Connection %s on port %d - %d second response time\n",
						(result == STATE_OK) ? "accepted" : "problem", server_port,
						(int) (end_time - start_time));
	}

	/* reset the alarm */
	alarm (0);

	return result;
}




/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"expect", required_argument, 0, 'e'},
		{"send", required_argument, 0, 's'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	if (argc < 2)
		usage ("\n");

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
#ifdef HAVE_GETOPT_H
		c =	getopt_long (argc, argv, "+hVvH:e:s:c:w:t:p:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+hVvH:e:s:c:w:t:p:");
#endif

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf ("%s: Unknown argument: %s\n\n", my_basename (argv[0]), optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'v':									/* verbose mode */
			verbose = TRUE;
			break;
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE)
				usage ("Invalid host name/address\n");
			server_address = optarg;
			break;
		case 'c':									/* critical */
			if (!is_intnonneg (optarg))
				usage ("Critical threshold must be a nonnegative integer\n");
			critical_time = atoi (optarg);
			check_critical_time = TRUE;
			break;
		case 'w':									/* warning */
			if (!is_intnonneg (optarg))
				usage ("Warning threshold must be a nonnegative integer\n");
			warning_time = atoi (optarg);
			check_warning_time = TRUE;
			break;
		case 't':									/* timeout */
			if (!is_intnonneg (optarg))
				usage ("Timeout interval must be a nonnegative integer\n");
			socket_timeout = atoi (optarg);
			break;
		case 'p':									/* port */
			if (!is_intnonneg (optarg))
				usage ("Serevr port must be a nonnegative integer\n");
			server_port = atoi (optarg);
			break;
		case 'e':									/* expect */
			server_expect = optarg;
			break;
		case 's':									/* send */
			server_send = optarg;
			break;
		}
	}

	c = optind;
	if (server_address == NULL && argv[c]) {
		if (is_host (argv[c]) == FALSE)
			usage ("Invalid host name/address\n");
		server_address = argv[c++];
	}
	else {
		usage ("Host name was not supplied\n");
	}

	return c;
}





void
print_usage (void)
{
	printf
		("Usage: %s -H <host_address> [-p port] [-w warn_time] [-c crit_time]\n"
		 "         [-e expect] [-s send] [-t to_sec] [-v]\n", PROGNAME);
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n\n"
		 "This plugin tests an UDP connection with the specified host.\n\n");
	print_usage ();
	printf
		("Options:\n"
		 " -H, --hostname=ADDRESS\n"
		 "    Host name argument for servers using host headers (use numeric\n"
		 "    address if possible to bypass DNS lookup).\n"
		 " -p, --port=INTEGER\n"
		 "    Port number\n"
		 " -e, --expect=STRING <optional>\n"
		 "    String to expect in first line of server response\n"
		 " -s, --send=STRING <optional>\n"
		 "    String to send to the server when initiating the connection\n"
		 " -w, --warning=INTEGER <optional>\n"
		 "    Response time to result in warning status (seconds)\n"
		 " -c, --critical=INTEGER <optional>\n"
		 "    Response time to result in critical status (seconds)\n"
		 " -t, --timeout=INTEGER <optional>\n"
		 "    Seconds before connection times out (default: %d)\n"
		 " -v, --verbose <optional>\n"
		 "    Show details for command-line debugging (do not use with nagios server)\n"
		 " -h, --help\n"
		 "    Print detailed help screen and exit\n"
		 " -V, --version\n"
		 "    Print version information and exit\n", DEFAULT_SOCKET_TIMEOUT);
}
