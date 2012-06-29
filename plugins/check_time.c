/*****************************************************************************
* 
* Nagios check_time plugin
* 
* License: GPL
* Copyright (c) 1999-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_time plugin
* 
* This plugin will check the time difference with the specified host.
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

const char *progname = "check_time";
const char *copyright = "1999-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum {
	TIME_PORT = 37
};

#define	UNIX_EPOCH 2208988800UL

uint32_t raw_server_time;
unsigned long server_time, diff_time;
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
int use_udp = FALSE;

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int sd;
	int result = STATE_UNKNOWN;
	time_t conntime;

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
	if (use_udp) {
		result = my_udp_connect (server_address, server_port, &sd);
	} else {
		result = my_tcp_connect (server_address, server_port, &sd);
	}

	if (result != STATE_OK) {
		if (check_critical_time == TRUE)
			result = STATE_CRITICAL;
		else if (check_warning_time == TRUE)
			result = STATE_WARNING;
		else
			result = STATE_UNKNOWN;
		die (result,
		           _("TIME UNKNOWN - could not connect to server %s, port %d\n"),
		           server_address, server_port);
	}

	if (use_udp) {
		if (send (sd, "", 0, 0) < 0) {
			if (check_critical_time == TRUE)
				result = STATE_CRITICAL;
			else if (check_warning_time == TRUE)
				result = STATE_WARNING;
			else
				result = STATE_UNKNOWN;
			die (result,
			  _("TIME UNKNOWN - could not send UDP request to server %s, port %d\n"),
			  server_address, server_port);
		}
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
		die (result,
							 _("TIME UNKNOWN - no data received from server %s, port %d\n"),
							 server_address, server_port);
	}

	result = STATE_OK;

	conntime = (end_time - start_time);
	if (check_critical_time == TRUE && conntime > critical_time)
		result = STATE_CRITICAL;
	else if (check_warning_time == TRUE && conntime > warning_time)
		result = STATE_WARNING;

	if (result != STATE_OK)
		die (result, _("TIME %s - %d second response time|%s\n"),
		     state_text (result), (int)conntime,
		     perfdata ("time", (long)conntime, "s",
		               check_warning_time, (long)warning_time,
		               check_critical_time, (long)critical_time,
		               TRUE, 0, FALSE, 0));

	server_time = ntohl (raw_server_time) - UNIX_EPOCH;
	if (server_time > (unsigned long)end_time)
		diff_time = server_time - (unsigned long)end_time;
	else
		diff_time = (unsigned long)end_time - server_time;

	if (check_critical_diff == TRUE && diff_time > critical_diff)
		result = STATE_CRITICAL;
	else if (check_warning_diff == TRUE && diff_time > warning_diff)
		result = STATE_WARNING;

	printf (_("TIME %s - %lu second time difference|%s %s\n"),
	        state_text (result), diff_time,
	        perfdata ("time", (long)conntime, "s",
	                  check_warning_time, (long)warning_time,
	                  check_critical_time, (long)critical_time,
	                  TRUE, 0, FALSE, 0),
	        perfdata ("offset", diff_time, "s",
	                  check_warning_diff, warning_diff,
	                  check_critical_diff, critical_diff,
	                  TRUE, 0, FALSE, 0));
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
		{"warning-variance", required_argument, 0, 'w'},
		{"critical-variance", required_argument, 0, 'c'},
		{"warning-connect", required_argument, 0, 'W'},
		{"critical-connect", required_argument, 0, 'C'},
		{"port", required_argument, 0, 'p'},
		{"udp", no_argument, 0, 'u'},
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
		c = getopt_long (argc, argv, "hVH:w:c:W:C:p:t:u", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'H':									/* hostname */
			if (is_host (optarg) == FALSE)
				usage2 (_("Invalid hostname/address"), optarg);
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
					usage4 (_("Warning thresholds must be a positive integer"));
				}
			}
			else {
				usage4 (_("Warning threshold must be a positive integer"));
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
					usage4 (_("Critical thresholds must be a positive integer"));
				}
			}
			else {
				usage4 (_("Critical threshold must be a positive integer"));
			}
			break;
		case 'W':									/* warning-connect */
			if (!is_intnonneg (optarg))
				usage4 (_("Warning threshold must be a positive integer"));
			else
				warning_time = atoi (optarg);
			check_warning_time = TRUE;
			break;
		case 'C':									/* critical-connect */
			if (!is_intnonneg (optarg))
				usage4 (_("Critical threshold must be a positive integer"));
			else
				critical_time = atoi (optarg);
			check_critical_time = TRUE;
			break;
		case 'p':									/* port */
			if (!is_intnonneg (optarg))
				usage4 (_("Port must be a positive integer"));
			else
				server_port = atoi (optarg);
			break;
		case 't':									/* timeout */
			if (!is_intnonneg (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				socket_timeout = atoi (optarg);
			break;
		case 'u':									/* udp */
			use_udp = TRUE;
		}
	}

	c = optind;
	if (server_address == NULL) {
		if (argc > c) {
			if (is_host (argv[c]) == FALSE)
				usage2 (_("Invalid hostname/address"), optarg);
			server_address = argv[c];
		}
		else {
			usage4 (_("Hostname was not supplied"));
		}
	}

	return OK;
}



void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", TIME_PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin will check the time on the specified host."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (" %s\n", "-u, --udp");
  printf ("   %s\n", _("Use UDP to connect, not TCP"));
  printf (" %s\n", "-w, --warning-variance=INTEGER");
  printf ("   %s\n", _("Time difference (sec.) necessary to result in a warning status"));
  printf (" %s\n", "-c, --critical-variance=INTEGER");
  printf ("   %s\n", _("Time difference (sec.) necessary to result in a critical status"));
  printf (" %s\n", "-W, --warning-connect=INTEGER");
  printf ("   %s\n", _("Response time (sec.) necessary to result in warning status"));
  printf (" %s\n", "-C, --critical-connect=INTEGER");
  printf ("   %s\n", _("Response time (sec.) necessary to result in critical status"));

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H <host_address> [-p port] [-u] [-w variance] [-c variance]\n",progname);
  printf (" [-W connect_time] [-C connect_time] [-t timeout]\n");
}
