/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

******************************************************************************/

const char *progname = "check_overcr";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
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
	int result;
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

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

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
			terminate (result, _("Unknown error fetching load data\n"));

		temp_ptr = (char *) strtok (recv_buffer, "\r\n");
		if (temp_ptr == NULL)
			terminate (STATE_CRITICAL, _("Invalid response from server - no load information\n"));
		load_1min = strtod (temp_ptr, NULL);

		temp_ptr = (char *) strtok (NULL, "\r\n");
		if (temp_ptr == NULL)
			terminate (STATE_CRITICAL, _("Invalid response from server after load 1\n"));
		load_5min = strtod (temp_ptr, NULL);

		temp_ptr = (char *) strtok (NULL, "\r\n");
		if (temp_ptr == NULL)
			terminate (STATE_CRITICAL, _("Invalid response from server after load 5\n"));
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

		terminate (result,
		          _("Load %s - %s-min load average = %0.2f"),
							 state_text(result),
		          temp_buffer,
		          load);

			break;

	case DPU:

		if (result != STATE_OK)
			terminate (result, _("Unknown error fetching disk data\n"));

		for (temp_ptr = (char *) strtok (recv_buffer, " ");
		     temp_ptr != NULL;
		     temp_ptr = (char *) strtok (NULL, " ")) {

			if (!strcmp (temp_ptr, disk_name)) {
				found_disk = TRUE;
				temp_ptr = (char *) strtok (NULL, "%");
				if (temp_ptr == NULL)
					terminate (STATE_CRITICAL, _("Invalid response from server\n"));
				percent_used_disk_space = strtoul (temp_ptr, NULL, 10);
				break;
			}

			temp_ptr = (char *) strtok (NULL, "\r\n");
		}

		/* error if we couldn't find the info for the disk */
		if (found_disk == FALSE)
			terminate (STATE_CRITICAL,
			           "Error: Disk '%s' non-existent or not mounted",
			           disk_name);

		if (check_critical_value == TRUE && (percent_used_disk_space >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (percent_used_disk_space >= warning_value))
			result = STATE_WARNING;

		terminate (result, "Disk %s - %lu%% used on %s", state_text(result), percent_used_disk_space, disk_name);

		break;

	case NETSTAT:

		if (result != STATE_OK)
			terminate (result, _("Unknown error fetching network status\n"));

		port_connections = strtod (recv_buffer, NULL);

		if (check_critical_value == TRUE && (port_connections >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (port_connections >= warning_value))
			result = STATE_WARNING;

		terminate (result,
		           _("Net %s - %d connection%s on port %d"),
		           state_text(result),
		           port_connections,
		           (port_connections == 1) ? "" : "s",
		           netstat_port);

		break;

	case PROCS:

		if (result != STATE_OK)
			terminate (result, _("Unknown error fetching process status\n"));

		temp_ptr = (char *) strtok (recv_buffer, "(");
		if (temp_ptr == NULL)
			terminate (STATE_CRITICAL, _("Invalid response from server\n"));

		temp_ptr = (char *) strtok (NULL, ")");
		if (temp_ptr == NULL)
			terminate (STATE_CRITICAL, _("Invalid response from server\n"));

		processes = strtod (temp_ptr, NULL);

		if (check_critical_value == TRUE && (processes >= critical_value))
			result = STATE_CRITICAL;
		else if (check_warning_value == TRUE && (processes >= warning_value))
			result = STATE_WARNING;

		terminate (result,
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

		terminate (result,
		           _("Uptime %s - Up %d days %d hours %d minutes"),
		           state_text(result),
		           uptime_days,
		           uptime_hours,
		           uptime_minutes);
		break;

	default:
		terminate (STATE_UNKNOWN, _("Nothing to check!\n"));
		break;
	}

	/* reset timeout */
	alarm (0);

	printf (_("Reached end of program with no data returned\n"));

	return result;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
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
		c = getopt_long (argc, argv, "+hVH:t:c:w:p:v:", long_options,
									 &option_index);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':									/* print short usage statement if args not parsable */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'H':									/* hostname */
			server_address = optarg;
			break;
		case 'p':									/* port */
			if (is_intnonneg (optarg))
				server_port = atoi (optarg);
			else
				terminate (STATE_UNKNOWN,
									 _("Server port an integer (seconds)\nType '%s -h' for additional help\n"),
									 progname);
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
print_usage (void)
{
	printf (_("\
Usage: %s -H host [-p port] [-v variable] [-w warning] [-c critical]\n\
  [-t timeout]\n"),
	        progname);
	printf (_(UT_HLP_VRS), progname, progname);
}

void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", PORT);

	print_revision (progname, revision);

	printf (_("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("\
This plugin attempts to contact the Over-CR collector daemon running on the\n\
remote UNIX server in order to gather the requested system information.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_("\
-v, --variable=STRING\n\
   Variable to check.  Valid variables include:\n\
     LOAD1         = 1 minute average CPU load\n\
     LOAD5         = 5 minute average CPU load\n\
     LOAD15        = 15 minute average CPU load\n\
     DPU<filesys>  = percent used disk space on filesystem <filesys>\n\
     PROC<process> = number of running processes with name <process>\n\
     NET<port>     = number of active connections on TCP port <port>\n\
     UPTIME        = system uptime in seconds\n"));

	printf (_("\
 -w, --warning=INTEGER\n\
   Threshold which will result in a warning status\n\
 -c, --critical=INTEGER\n\
   Threshold which will result in a critical status\n"));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_("\
Notes:\n\
 - For the available options, the critical threshold value should always be\n\
   higher than the warning threshold value, EXCEPT with the uptime variable\n\n"));

	printf (_("\
 - This plugin requres that Eric Molitors' Over-CR collector daemon be\n\
   running on the remote server. Over-CR can be downloaded from\n\
   http://www.molitor.org/overcr (This plugin was tested with version\n\
   0.99.53 of the Over-CR collector)\n\n"));

	printf (_(UT_SUPPORT));
}
