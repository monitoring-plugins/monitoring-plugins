/******************************************************************************
*
* CHECK_SMTP.C
*
* Program: SMTP plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* $Id$
*
* Description:
*
* This plugin will attempt to open an SMTP connection with the host.
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

#define PROGNAME "check_smtp"

#define SMTP_PORT	25
#define SMTP_EXPECT     "220"
/* sendmail will syslog a "NOQUEUE" error if session does not attempt
 * to do something useful. This can be prevented by giving a command
 * even if syntax is illegal (MAIL requires a FROM:<...> argument)
 * You can disable sending DUMMYCMD by undefining SMTP_USE_DUMMYCMD.
 */
#define SMTP_DUMMYCMD  "MAIL\r\n"
#define SMTP_USE_DUMMYCMD 1
#define SMTP_QUIT	"QUIT\r\n"

int process_arguments (int, char **);
int call_getopt (int, char **);
int validate_arguments (void);
int check_disk (int usp, int free_disk);
void print_help (void);
void print_usage (void);

int server_port = SMTP_PORT;
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
	char buffer[MAX_INPUT_BUFFER] = "";

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	time (&start_time);
	result = my_tcp_connect (server_address, server_port, &sd);

	/* we connected, so close connection before exiting */
	if (result == STATE_OK) {

		/* watch for the SMTP connection string */
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
				if (server_port == SMTP_PORT)
					printf ("Invalid SMTP response received from host\n");
				else
					printf ("Invalid SMTP response received from host on port %d\n",
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
					printf ("SMTP %s - %d sec. response time, %s\n",
									state_text (result), (int) (end_time - start_time), buffer);
				else
					printf ("SMTP %s - %d second response time\n", state_text (result),
									(int) (end_time - start_time));
			}
		}

		/* close the connection */
#ifdef SMTP_USE_DUMMYCMD
               send(sd,SMTP_DUMMYCMD,strlen(SMTP_DUMMYCMD),0);
               /* allow for response to DUMMYCMD to reach us */
               recv(sd,buffer,MAX_INPUT_BUFFER-1,0);
#endif /* SMTP_USE_DUMMYCMD */

		send (sd, SMTP_QUIT, strlen (SMTP_QUIT), 0);
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
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}



	c = 0;
	while ((c += (call_getopt (argc - c, &argv[c]))) < argc) {

		if (is_option (argv[c]))
			continue;

		if (server_address == NULL) {
			if (is_host (argv[c])) {
				server_address = argv[c];
			}
			else {
				usage ("Invalid host name");
			}
		}
	}

	if (server_address == NULL)
		server_address = strscpy (NULL, "127.0.0.1");

	if (server_expect == NULL)
		server_expect = strscpy (NULL, SMTP_EXPECT);

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
		{"expect", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"port", required_argument, 0, 'P'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVvt:p:e:c:w:H:", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+?hVvt:p:e:c:w:H:");
#endif

		i++;

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 't':
		case 'p':
		case 'e':
		case 'c':
		case 'w':
		case 'H':
			i++;
		}

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage ("Invalid host name\n");
			}
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage ("Server port must be a positive integer\n");
			}
			break;
		case 'e':									/* username */
			server_expect = optarg;
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
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_time = atoi (optarg);
				check_warning_time = TRUE;
			}
			else {
				usage ("Warning time must be a nonnegative integer\n");
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
		("Copyright (c) 2000 Ethan Galstad/Karl DeBisschop\n\n"
		 "This plugin test the SMTP service on the specified host.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -H, --hostname=STRING or IPADDRESS\n"
		 "   Check server on the indicated host\n"
		 " -p, --port=INTEGER\n"
		 "   Make connection on the indicated port (default: %d)\n"
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
		 SMTP_PORT, SMTP_EXPECT, DEFAULT_SOCKET_TIMEOUT);
	support ();
}





void
print_usage (void)
{
	printf
		("Usage: %s -H host [-e expect] [-p port] [-w warn] [-c crit] [-t timeout] [-v]\n"
		 "       %s --help\n"
		 "       %s --version\n", PROGNAME, PROGNAME, PROGNAME);
}
