/******************************************************************************
 *
 * CHECK_FTP.C
 *
 * Program: FTP plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 *
 * $Id$
 *
 * Description:
 *
 * This plugin will attempt to open an FTP connection with the host.
 * Successul connects return STATE_OK, refusals and timeouts return
 * STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful
 * connects, but incorrect reponse messages from the host result in
 * STATE_WARNING return values.
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

#define PROGNAME "check_ftp"

#define FTP_PORT	21
#define FTP_EXPECT      "220"
#define FTP_QUIT	"QUIT\n"

int process_arguments (int, char **);
int call_getopt (int, char **);
void print_usage (void);
void print_help (void);

time_t start_time, end_time;
int server_port = FTP_PORT;
char *server_address = NULL;
char *server_expect = NULL;
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

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	time (&start_time);
	result = my_tcp_connect (server_address, server_port, &sd);

	/* we connected, so close connection before exiting */
	if (result == STATE_OK) {

		/* watch for the FTP connection string */
		result = recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0);

		/* strip the buffer of carriage returns */
		strip (buffer);

		/* return a WARNING status if we couldn't read any data */
		if (result == -1) {
			printf ("recv() failed\n");
			result = STATE_WARNING;
		}

		else {

			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {

				if (server_port == FTP_PORT)
					printf ("Invalid FTP response received from host\n");
				else
					printf ("Invalid FTP response received from host on port %d\n",
									server_port);
				result = STATE_WARNING;
			}

			else {
				time (&end_time);

				result = STATE_OK;

				if (check_critical_time == TRUE
						&& (end_time - start_time) > critical_time) result =
						STATE_CRITICAL;
				else if (check_warning_time == TRUE
								 && (end_time - start_time) > warning_time) result =
						STATE_WARNING;

				if (verbose == TRUE)
					printf ("FTP %s - %d sec. response time, %s\n",
									(result == STATE_OK) ? "ok" : "problem",
									(int) (end_time - start_time), buffer);
				else
					printf ("FTP %s - %d second response time\n",
									(result == STATE_OK) ? "ok" : "problem",
									(int) (end_time - start_time));
			}
		}

		/* close the connection */
		send (sd, FTP_QUIT, strlen (FTP_QUIT), 0);
		close (sd);
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

	c = 0;
	while ((c += call_getopt (argc - c, &argv[c])) < argc) {

		if (is_option (argv[c]))
			continue;

		if (server_address == NULL) {
			if (argc > c) {
				if (is_host (argv[c]) == FALSE)
					usage ("Invalid host name/address\n");
				server_address = argv[c];
			}
			else {
				usage ("Host name was not supplied\n");
			}
		}
	}

	if (server_expect == NULL)
		server_expect = strscpy (NULL, FTP_EXPECT);

	return OK;
}





int
call_getopt (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"hostname", required_argument, 0, 'H'},
		{"expect", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 'w'},
		{"port", required_argument, 0, 'p'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVvH:e:c:w:t:p:", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+hVvH:e:c:w:t:p:");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'H':
		case 'e':
		case 'c':
		case 'w':
		case 't':
		case 'p':
			i++;
		}

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
		case 'e':									/* expect */
			server_expect = optarg;
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
		}
	}
	return i;
}





void
print_usage (void)
{
	printf
		("Usage: %s -H <host_address> [-e expect] [-p port] [-w warn_time]\n"
		 "         [-c crit_time] [-t to_sec] [-v]\n", PROGNAME);
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n\n"
		 "This plugin tests an FTP connection with the specified host.\n\n");
	print_usage ();
	printf
		("Options:\n"
		 " -H, --hostname=ADDRESS\n"
		 "    Host name argument for servers using host headers (use numeric\n"
		 "    address if possible to bypass DNS lookup).\n"
		 " -e, --expect=STRING\n"
		 "    String to expect in first line of server response (default: %s)\n"
		 " -p, --port=INTEGER\n"
		 "    Port number (default: %d)\n"
		 " -w, --warning=INTEGER\n"
		 "    Response time to result in warning status (seconds)\n"
		 " -c, --critical=INTEGER\n"
		 "    Response time to result in critical status (seconds)\n"
		 " -t, --timeout=INTEGER\n"
		 "    Seconds before connection times out (default: %d)\n"
		 " -v"
		 "    Show details for command-line debugging (do not use with nagios server)\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n"
		 "    Print version information\n",
		 FTP_EXPECT, FTP_PORT, DEFAULT_SOCKET_TIMEOUT);
}
