/*****************************************************************************
* 
* Nagios check_overcr plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_overcr plugin
* 
* This plugin attempts to contact the Over-CR collector daemon running on the
* remote UNIX server in order to gather the requested system information.
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

const char *progname = "check_overcr";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum checkvar {
	NONE,
	LOAD1,
	LOAD5,
	LOAD15,
	DPU,
	PROCS,
	NETSTAT,
	UPTIME
};

enum {
	PORT = 2000
};

char *server_address = NULL;
int server_port = PORT;
double warning_value = 0L;
double critical_value = 0L;
int check_warning_value = FALSE;
int check_critical_value = FALSE;
enum checkvar vars_to_check = NONE;
int cmd_timeout = 1;

int netstat_port = 0;
char *disk_name = NULL;
char *process_name = NULL;
	char send_buffer[MAX_INPUT_BUFFER];

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	char recv_buffer[MAX_INPUT_BUFFER];
	char temp_buffer[MAX_INPUT_BUFFER];
	char *temp_ptr = NULL;
	int found_disk = FALSE;
	unsigned long percent_used_disk_space = 100;
	double load;
	double load_1min;
	double load_5min;
	double load_15min;
	int port_connections = 0;
	int processes = 0;
	double uptime_raw_hours;
	int uptime_raw_minutes = 0;
	int uptime_days = 0;
	int uptime_hours = 0;
	int uptime_minutes = 0;

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

	result = process_tcp_request2 (server_address,
	                               server_port,
	                               send_buffer,
	                               recv_buffer,
	                               sizeof (recv_buffer));

	switch (vars_to_check) {

	case LOAD1:
	case LOAD5:
	case LOAD15:
	
		if (result != STATE_OK)
			die (result, _("Unknown error fetching load data\n"));

		temp_ptr = (char *) strtok (recv_buffer, "\r\n");
		if (temp_ptr == NULL)
			die (STATE_CRITICAL, _("Invalid response from server - no load information\n"));
		else
			load_1min = strtod (temp_ptr, NULL);

		temp_ptr = (char *) strtok (NULL, "\r\n");
		if (temp_ptr == NULL)
			die (STATE_CRITICAL, _("Invalid response from server after load 1\n"));
		else
			load_5min = strtod (temp_ptr, NULL);

		temp_ptr = (char *) strtok (NULL, "\r\n");
		if (temp_ptr == NULL)
			die (STATE_CRITICAL, _("Invalid response from server after load 5\n"));
		else
			load_15min = strtod (temp_ptr, NULL);

		switch (vars_to_check) {
		case LOAD1:
			strcpy (temp_buffer, "1");
			load = load_1min;
			break;
		case LOAD5:
			strcpy (temp_buffer, "5");
			load = load_5min;
			break;
		default:
			strcpy (temp_buffer, "15");
			load = load_15min;
			break;
		}

		if (check_critical_value == TRUE && (load >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (load >= warning_value))
			result = STATE_WARNING;

		die (result,
		          _("Load %s - %s-min load average = %0.2f"),
							 state_text(result),
		          temp_buffer,
		          load);

			break;

	case DPU:

		if (result != STATE_OK)
			die (result, _("Unknown error fetching disk data\n"));

		for (temp_ptr = (char *) strtok (recv_buffer, " ");
		     temp_ptr != NULL;
		     temp_ptr = (char *) strtok (NULL, " ")) {

			if (!strcmp (temp_ptr, disk_name)) {
				found_disk = TRUE;
				temp_ptr = (char *) strtok (NULL, "%");
				if (temp_ptr == NULL)
					die (STATE_CRITICAL, _("Invalid response from server\n"));
				else
					percent_used_disk_space = strtoul (temp_ptr, NULL, 10);
				break;
			}

			temp_ptr = (char *) strtok (NULL, "\r\n");
		}

		/* error if we couldn't find the info for the disk */
		if (found_disk == FALSE)
			die (STATE_CRITICAL,
			           "CRITICAL - Disk '%s' non-existent or not mounted",
			           disk_name);

		if (check_critical_value == TRUE && (percent_used_disk_space >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (percent_used_disk_space >= warning_value))
			result = STATE_WARNING;

		die (result, "Disk %s - %lu%% used on %s", state_text(result), percent_used_disk_space, disk_name);

		break;

	case NETSTAT:

		if (result != STATE_OK)
			die (result, _("Unknown error fetching network status\n"));
		else
			port_connections = strtod (recv_buffer, NULL);

		if (check_critical_value == TRUE && (port_connections >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (port_connections >= warning_value))
			result = STATE_WARNING;

		die (result,
		           _("Net %s - %d connection%s on port %d"),
		           state_text(result),
		           port_connections,
		           (port_connections == 1) ? "" : "s",
		           netstat_port);

		break;

	case PROCS:

		if (result != STATE_OK)
			die (result, _("Unknown error fetching process status\n"));

		temp_ptr = (char *) strtok (recv_buffer, "(");
		if (temp_ptr == NULL)
			die (STATE_CRITICAL, _("Invalid response from server\n"));

		temp_ptr = (char *) strtok (NULL, ")");
		if (temp_ptr == NULL)
			die (STATE_CRITICAL, _("Invalid response from server\n"));
		else
			processes = strtod (temp_ptr, NULL);

		if (check_critical_value == TRUE && (processes >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (processes >= warning_value))
			result = STATE_WARNING;

		die (result,
		           _("Process %s - %d instance%s of %s running"),
		           state_text(result),
		           processes,
		           (processes == 1) ? "" : "s",
		           process_name);
		break;

	case UPTIME:

		if (result != STATE_OK)
			return result;

		uptime_raw_hours = strtod (recv_buffer, NULL);
		uptime_raw_minutes = (unsigned long) (uptime_raw_hours * 60.0);

		if (check_critical_value == TRUE && (uptime_raw_minutes <= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (uptime_raw_minutes <= warning_value))
			result = STATE_WARNING;

		uptime_days = uptime_raw_minutes / 1440;
		uptime_raw_minutes %= 1440;
		uptime_hours = uptime_raw_minutes / 60;
		uptime_raw_minutes %= 60;
		uptime_minutes = uptime_raw_minutes;

		die (result,
		           _("Uptime %s - Up %d days %d hours %d minutes"),
		           state_text(result),
		           uptime_days,
		           uptime_hours,
		           uptime_minutes);
		break;

	default:
		die (STATE_UNKNOWN, _("Nothing to check!\n"));
		break;
	}
}


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"port", required_argument, 0, 'p'},
		{"timeout", required_argument, 0, 't'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"variable", required_argument, 0, 'v'},
		{"hostname", required_argument, 0, 'H'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	/* no options were supplied */
	if (argc < 2)
		return ERROR;

	/* backwards compatibility */
	if (!is_option (argv[1])) {
		server_address = argv[1];
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wv", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-cv", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVH:t:c:w:p:v:", longopts,
									 &option);

		if (c == -1 || c == EOF || c == 1)
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
			server_address = optarg;
			break;
		case 'p':									/* port */
			if (is_intnonneg (optarg))
				server_port = atoi (optarg);
			else
				die (STATE_UNKNOWN,
									 _("Server port an integer\n"));
			break;
		case 'v':									/* variable */
			if (strcmp (optarg, "LOAD") == 0) {
				strcpy (send_buffer, "LOAD\r\nQUIT\r\n");
				if (strcmp (optarg, "LOAD1") == 0)
					vars_to_check = LOAD1;
				else if (strcmp (optarg, "LOAD5") == 0)
					vars_to_check = LOAD5;
				else if (strcmp (optarg, "LOAD15") == 0)
					vars_to_check = LOAD15;
			}
			else if (strcmp (optarg, "UPTIME") == 0) {
				vars_to_check = UPTIME;
				strcpy (send_buffer, "UPTIME\r\n");
			}
			else if (strstr (optarg, "PROC") == optarg) {
				vars_to_check = PROCS;
				process_name = strscpy (process_name, optarg + 4);
				sprintf (send_buffer, "PROCESS %s\r\n", process_name);
			}
			else if (strstr (optarg, "NET") == optarg) {
				vars_to_check = NETSTAT;
				netstat_port = atoi (optarg + 3);
				sprintf (send_buffer, "NETSTAT %d\r\n", netstat_port);
			}
			else if (strstr (optarg, "DPU") == optarg) {
				vars_to_check = DPU;
				strcpy (send_buffer, "DISKSPACE\r\n");
				disk_name = strscpy (disk_name, optarg + 3);
			}
			else
				return ERROR;
			break;
		case 'w':									/* warning threshold */
			warning_value = strtoul (optarg, NULL, 10);
			check_warning_value = TRUE;
			break;
		case 'c':									/* critical threshold */
			critical_value = strtoul (optarg, NULL, 10);
			check_critical_value = TRUE;
			break;
		case 't':									/* timeout */
			socket_timeout = atoi (optarg);
			if (socket_timeout <= 0)
				return ERROR;
		}

	}
	return OK;
}


void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin attempts to contact the Over-CR collector daemon running on the"));
  printf ("%s\n", _("remote UNIX server in order to gather the requested system information."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

  printf (" %s\n", "-w, --warning=INTEGER");
  printf ("    %s\n", _("Threshold which will result in a warning status"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("    %s\n", _("Threshold which will result in a critical status"));
  printf (" %s\n", "-v, --variable=STRING");
  printf ("    %s\n", _("Variable to check.  Valid variables include:"));
  printf ("    %s\n", _("LOAD1         = 1 minute average CPU load"));
  printf ("    %s\n", _("LOAD5         = 5 minute average CPU load"));
  printf ("    %s\n", _("LOAD15        = 15 minute average CPU load"));
  printf ("    %s\n", _("DPU<filesys>  = percent used disk space on filesystem <filesys>"));
  printf ("    %s\n", _("PROC<process> = number of running processes with name <process>"));
  printf ("    %s\n", _("NET<port>     = number of active connections on TCP port <port>"));
  printf ("    %s\n", _("UPTIME        = system uptime in seconds"));

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf (UT_VERBOSE);

  printf ("\n");
  printf ("%s\n", _("This plugin requires that Eric Molitors' Over-CR collector daemon be"));
  printf ("%s\n", _("running on the remote server."));
  printf ("%s\n", _("Over-CR can be downloaded from http://www.molitor.org/overcr"));
  printf ("%s\n", _("This plugin was tested with version 0.99.53 of the Over-CR collector"));

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("For the available options, the critical threshold value should always be"));
  printf (" %s\n", _("higher than the warning threshold value, EXCEPT with the uptime variable"));

  printf (UT_SUPPORT);
}


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host [-p port] [-v variable] [-w warning] [-c critical] [-t timeout]\n", progname);
}
