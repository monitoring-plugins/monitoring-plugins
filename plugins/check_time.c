/******************************************************************************
*
* CHECK_TIME.C
*
* Program: Network time server plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
*
* $Id$
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

const char *progname = "check_time";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2002"
#define AUTHOR "Ethan Galstad"
#define EMAIL "nagios@nagios.org"
#define SUMMARY "Check time on the specified host.\n"

#define TIME_PORT	37
#define UNIX_EPOCH	2208988800UL

unsigned long server_time, raw_server_time;
time_t diff_time;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
unsigned long warning_diff = 0;
int check_warning_diff = FALSE;
unsigned long critical_diff = 0;
int check_critical_diff = FALSE;
int server_port = TIME_PORT;
char *server_address = NULL;


int process_arguments (int, char **);
void print_usage (void);
void print_help (void);


int
main (int argc, char **argv)
{
	int sd;
	int result;

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);
	time (&start_time);

	/* try to connect to the host at the given port number */
	if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK) {
		if (check_critical_time == TRUE)
			result = STATE_CRITICAL;
		else if (check_warning_time == TRUE)
			result = STATE_WARNING;
		else
			result = STATE_UNKNOWN;
		terminate (result,
		           "TIME UNKNOWN - could not connect to server %s, port %d\n",
		           server_address, server_port);
	}

	/* watch for the connection string */
	result = recv (sd, (void *)&raw_server_time, sizeof (raw_server_time), 0);

	/* close the connection */
	close (sd);

	/* reset the alarm */
	time (&end_time);
	alarm (0);

	/* return a WARNING status if we couldn't read any data */
	if (result <= 0) {
		if (check_critical_time == TRUE)
			result = STATE_CRITICAL;
		else if (check_warning_time == TRUE)
			result = STATE_WARNING;
		else
			result = STATE_UNKNOWN;
		terminate (result,
							 "TIME UNKNOWN - no data on recv() from server %s, port %d\n",
							 server_address, server_port);
	}

	result = STATE_OK;

	if (check_critical_time == TRUE && (end_time - start_time) > critical_time)
		result = STATE_CRITICAL;
	else if (check_warning_time == TRUE
					 && (end_time - start_time) > warning_time) result = STATE_WARNING;

	if (result != STATE_OK)
		terminate (result, "TIME %s - %d second response time\n",
							 state_text (result), (int) (end_time - start_time));

	server_time = ntohl (raw_server_time) - UNIX_EPOCH;
	if (server_time > end_time)
		diff_time = server_time - end_time;
	else
		diff_time = end_time - server_time;

	if (check_critical_diff == TRUE && diff_time > critical_diff)
		result = STATE_CRITICAL;
	else if (check_warning_diff == TRUE && diff_time > warning_diff)
		result = STATE_WARNING;

	printf ("TIME %s - %lu second time difference\n", state_text (result),
					diff_time);
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
		{"warning-variance", required_argument, 0, 'w'},
		{"critical-variance", required_argument, 0, 'c'},
		{"warning-connect", required_argument, 0, 'W'},
		{"critical-connect", required_argument, 0, 'C'},
		{"port", required_argument, 0, 'p'},
		{"timeout", required_argument, 0, 't'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage ("\n");

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wd", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-cd", argv[c]) == 0)
			strcpy (argv[c], "-c");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-W");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-C");
	}

	while (1) {
		c = getopt_long (argc, argv, "hVH:w:c:W:C:p:t:", long_options,
									 &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage3 ("Unknown argument", optopt);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE)
				usage ("Invalid host name/address\n");
			server_address = optarg;
			break;
		case 'w':									/* warning-variance */
			if (is_intnonneg (optarg)) {
				warning_diff = strtoul (optarg, NULL, 10);
				check_warning_diff = TRUE;
			}
			else if (strspn (optarg, "0123456789:,") > 0) {
				if (sscanf (optarg, "%lu%*[:,]%d", &warning_diff, &warning_time) == 2) {
					check_warning_diff = TRUE;
					check_warning_time = TRUE;
				}
				else {
					usage ("Warning thresholds must be a nonnegative integer\n");
				}
			}
			else {
				usage ("Warning threshold must be a nonnegative integer\n");
			}
			break;
		case 'c':									/* critical-variance */
			if (is_intnonneg (optarg)) {
				critical_diff = strtoul (optarg, NULL, 10);
				check_critical_diff = TRUE;
			}
			else if (strspn (optarg, "0123456789:,") > 0) {
				if (sscanf (optarg, "%lu%*[:,]%d", &critical_diff, &critical_time) ==
						2) {
					check_critical_diff = TRUE;
					check_critical_time = TRUE;
				}
				else {
					usage ("Critical thresholds must be a nonnegative integer\n");
				}
			}
			else {
				usage ("Critical threshold must be a nonnegative integer\n");
			}
			break;
		case 'W':									/* warning-connect */
			if (!is_intnonneg (optarg))
				usage ("Warning threshold must be a nonnegative integer\n");
			warning_time = atoi (optarg);
			check_warning_time = TRUE;
			break;
		case 'C':									/* critical-connect */
			if (!is_intnonneg (optarg))
				usage ("Critical threshold must be a nonnegative integer\n");
			critical_time = atoi (optarg);
			check_critical_time = TRUE;
			break;
		case 'p':									/* port */
			if (!is_intnonneg (optarg))
				usage ("Serevr port must be a nonnegative integer\n");
			server_port = atoi (optarg);
			break;
		case 't':									/* timeout */
			if (!is_intnonneg (optarg))
				usage ("Timeout interval must be a nonnegative integer\n");
			socket_timeout = atoi (optarg);
			break;
		}
	}

	c = optind;
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

	return OK;
}





void
print_usage (void)
{
	printf
		("Usage:\n"
		 " %s -H <host_address> [-p port] [-w variance] [-c variance]\n"
		 "           [-W connect_time] [-C connect_time] [-t timeout]\n"
		 " %s (-h | --help) for detailed help\n"
		 " %s (-V | --version) for version information\n",
		 progname, progname, progname);
}





void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("Options:\n"
		 " -H, --hostname=ADDRESS\n"
		 "    Host name argument for servers using host headers (use numeric\n"
		 "    address if possible to bypass DNS lookup).\n"
		 " -w, --warning-variance=INTEGER\n"
		 "    Time difference (sec.) necessary to result in a warning status\n"
		 " -c, --critical-variance=INTEGER\n"
		 "    Time difference (sec.) necessary to result in a critical status\n"
		 " -W, --warning-connect=INTEGER\n"
		 "    Response time (sec.) necessary to result in warning status\n"
		 " -C, --critical-connect=INTEGER\n"
		 "    Response time (sec.) necessary to result in critical status\n"
		 " -t, --timeout=INTEGER\n"
		 "    Seconds before connection times out (default: %d)\n"
		 " -p, --port=INTEGER\n"
		 "    Port number (default: %d)\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n"
		 "    Print version information\n\n", DEFAULT_SOCKET_TIMEOUT, TIME_PORT);
	support ();
}
