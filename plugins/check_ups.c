/******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or (at
 your option) any later version.

 This program is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

******************************************************************************/

const char *progname = "check_ups";
const char *revision = "$Revision$";
const char *copyright = "2000-2002";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

enum {
	PORT = 3493
};

#define CHECK_NONE	 0

#define UPS_NONE     0   /* no supported options */
#define UPS_UTILITY  1   /* supports utility line voltage */
#define UPS_BATTPCT  2   /* supports percent battery remaining */
#define UPS_STATUS   4   /* supports UPS status */
#define UPS_TEMP     8   /* supports UPS temperature */
#define UPS_LOADPCT	16   /* supports load percent */

#define UPSSTATUS_NONE     0
#define UPSSTATUS_OFF      1
#define UPSSTATUS_OL       2
#define UPSSTATUS_OB       4
#define UPSSTATUS_LB       8
#define UPSSTATUS_CAL     16
#define UPSSTATUS_RB      32  /*Replace Battery */
#define UPSSTATUS_UNKOWN  64

int server_port = PORT;
char *server_address;
char *ups_name = NULL;
double warning_value = 0.0;
double critical_value = 0.0;
int check_warn = FALSE;
int check_crit = FALSE;
int check_variable = UPS_NONE;
int supported_options = UPS_NONE;
int status = UPSSTATUS_NONE;

double ups_utility_voltage = 0.0;
double ups_battery_percent = 0.0;
double ups_load_percent = 0.0;
double ups_temperature = 0.0;
char *ups_status;

int determine_status (void);
int determine_supported_vars (void);
int get_ups_variable (const char *, char *, size_t);

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int result = STATE_OK;
	char *message;
	char *data;
	char temp_buffer[MAX_INPUT_BUFFER];
	double ups_utility_deviation = 0.0;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	ups_status = strdup ("N/A");
	data = strdup ("");

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* determine what variables the UPS supports */
	if (determine_supported_vars () != OK)
		return STATE_CRITICAL;

	/* get the ups status if possible */
	if (supported_options & UPS_STATUS) {

		if (determine_status () != OK)
			return STATE_CRITICAL;
		ups_status = strdup ("");
		result = STATE_OK;

		if (status & UPSSTATUS_OFF) {
			asprintf (&ups_status, "Off");
			result = STATE_CRITICAL;
		}
		else if ((status & (UPSSTATUS_OB | UPSSTATUS_LB)) ==
						 (UPSSTATUS_OB | UPSSTATUS_LB)) {
			asprintf (&ups_status, "On Battery, Low Battery");
			result = STATE_CRITICAL;
		}
		else {
			if (status & UPSSTATUS_OL) {
				asprintf (&ups_status, "%s%s", ups_status, "Online");
			}
			if (status & UPSSTATUS_OB) {
				asprintf (&ups_status, "%s%s", ups_status, "On Battery");
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_LB) {
				asprintf (&ups_status, "%s%s", ups_status, ", Low Battery");
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_CAL) {
				asprintf (&ups_status, "%s%s", ups_status, ", Calibrating");
			}
			if (status & UPSSTATUS_RB) {
				asprintf (&ups_status, "%s%s", ups_status, ", Replace Battery");
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_UNKOWN) {
				asprintf (&ups_status, "%s%s", ups_status, ", Unknown");
			}
		}
		asprintf (&message, "%sStatus=%s ", message, ups_status);
	}

	/* get the ups utility voltage if possible */
	if (supported_options & UPS_UTILITY) {

		if (get_ups_variable ("UTILITY", temp_buffer, sizeof (temp_buffer)) != OK)
			return STATE_CRITICAL;

		ups_utility_voltage = atof (temp_buffer);
		asprintf (&message, "%sUtility=%3.1fV ", message, ups_utility_voltage);

		if (ups_utility_voltage > 120.0)
			ups_utility_deviation = 120.0 - ups_utility_voltage;
		else
			ups_utility_deviation = ups_utility_voltage - 120.0;

		if (check_variable == UPS_UTILITY) {
			if (check_crit==TRUE && ups_utility_deviation>=critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn==TRUE && ups_utility_deviation>=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			asprintf (&data, "%s",
			          perfdata ("voltage", (long)(1000*ups_utility_voltage), "mV",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, FALSE, 0));
		} else {
			asprintf (&data, "%s",
			          perfdata ("voltage", (long)(1000*ups_utility_voltage), "mV",
			                    FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
		}
	}

	/* get the ups battery percent if possible */
	if (supported_options & UPS_BATTPCT) {

		if (get_ups_variable ("BATTPCT", temp_buffer, sizeof (temp_buffer)) != OK)
			return STATE_CRITICAL;

		ups_battery_percent = atof (temp_buffer);
		asprintf (&message, "%sBatt=%3.1f%% ", message, ups_battery_percent);

		if (check_variable == UPS_BATTPCT) {
			if (check_crit==TRUE && ups_battery_percent <= critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn==TRUE && ups_battery_percent<=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			asprintf (&data, "%s %s", data,
			          perfdata ("battery", (long)ups_battery_percent, "%",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, TRUE, 100));
		} else {
			asprintf (&data, "%s %s", data,
			          perfdata ("battery", (long)ups_battery_percent, "%",
			                    FALSE, 0, FALSE, 0, TRUE, 0, TRUE, 100));
		}
	}

	/* get the ups load percent if possible */
	if (supported_options & UPS_LOADPCT) {

		if (get_ups_variable ("LOADPCT", temp_buffer, sizeof (temp_buffer)) != OK)
			return STATE_CRITICAL;

		ups_load_percent = atof (temp_buffer);
		asprintf (&message, "%sLoad=%3.1f%% ", message, ups_load_percent);

		if (check_variable == UPS_LOADPCT) {
			if (check_crit==TRUE && ups_load_percent>=critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn==TRUE && ups_load_percent>=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			asprintf (&data, "%s %s", data,
			          perfdata ("load", (long)ups_load_percent, "%",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, TRUE, 100));
		} else {
			asprintf (&data, "%s %s", data,
			          perfdata ("load", (long)ups_load_percent, "%",
			                    FALSE, 0, FALSE, 0, TRUE, 0, TRUE, 100));
		}
	}

	/* get the ups temperature if possible */
	if (supported_options & UPS_TEMP) {

		if (get_ups_variable ("UPSTEMP", temp_buffer, sizeof (temp_buffer)) != OK)
			return STATE_CRITICAL;

		ups_temperature = (atof (temp_buffer) * 1.8) + 32;
		asprintf (&message, "%sTemp=%3.1fF", message, ups_temperature);

		if (check_variable == UPS_TEMP) {
			if (check_crit==TRUE && ups_temperature>=critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn == TRUE && ups_temperature>=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			asprintf (&data, "%s %s", data,
			          perfdata ("temp", (long)ups_temperature, "degF",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, FALSE, 0));
		} else {
			asprintf (&data, "%s %s", data,
			          perfdata ("temp", (long)ups_temperature, "degF",
			                    FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
		}
	}

	/* if the UPS does not support any options we are looking for, report an error */
	if (supported_options == UPS_NONE) {
		result = STATE_CRITICAL;
		asprintf (&message, "UPS does not support any available options\n");
	}

	/* reset timeout */
	alarm (0);

	printf ("UPS %s - %s|%s\n", state_text(result), message, data);
	return result;
}



/* determines what options are supported by the UPS */
int
determine_status (void)
{
	char recv_buffer[MAX_INPUT_BUFFER];
	char temp_buffer[MAX_INPUT_BUFFER];
	char *ptr;

	if (get_ups_variable ("STATUS", recv_buffer, sizeof (recv_buffer)) !=
			STATE_OK) {
		printf ("Invalid response received from hostn");
		return ERROR;
	}

	recv_buffer[strlen (recv_buffer) - 1] = 0;

	strcpy (temp_buffer, recv_buffer);
	for (ptr = (char *) strtok (temp_buffer, " "); ptr != NULL;
			 ptr = (char *) strtok (NULL, " ")) {
		if (!strcmp (ptr, "OFF"))
			status |= UPSSTATUS_OFF;
		else if (!strcmp (ptr, "OL"))
			status |= UPSSTATUS_OL;
		else if (!strcmp (ptr, "OB"))
			status |= UPSSTATUS_OB;
		else if (!strcmp (ptr, "LB"))
			status |= UPSSTATUS_LB;
		else if (!strcmp (ptr, "CAL"))
			status |= UPSSTATUS_CAL;
		else if (!strcmp (ptr, "RB"))
			status |= UPSSTATUS_RB;
		else
			status |= UPSSTATUS_UNKOWN;
	}

	return OK;
}


/* determines what options are supported by the UPS */
int
determine_supported_vars (void)
{
	char send_buffer[MAX_INPUT_BUFFER];
	char recv_buffer[MAX_INPUT_BUFFER];
	char temp_buffer[MAX_INPUT_BUFFER];
	char *ptr;


	/* get the list of variables that this UPS supports */
	if (ups_name)
		sprintf (send_buffer, "LISTVARS %s\r\n", ups_name);
	else
		sprintf (send_buffer, "LISTVARS\r\n");
	if (process_tcp_request
			(server_address, server_port, send_buffer, recv_buffer,
			 sizeof (recv_buffer)) != STATE_OK) {
		printf ("Invalid response received from host\n");
		return ERROR;
	}

	recv_buffer[strlen (recv_buffer) - 1] = 0;

	if (ups_name)
		ptr = recv_buffer + 5 + strlen (ups_name) + 2;
	else
		ptr = recv_buffer + 5;

	strcpy (temp_buffer, recv_buffer);

	for (ptr = (char *) strtok (temp_buffer, " "); ptr != NULL;
			 ptr = (char *) strtok (NULL, " ")) {
		if (!strcmp (ptr, "UTILITY"))
			supported_options |= UPS_UTILITY;
		else if (!strcmp (ptr, "BATTPCT"))
			supported_options |= UPS_BATTPCT;
		else if (!strcmp (ptr, "LOADPCT"))
			supported_options |= UPS_LOADPCT;
		else if (!strcmp (ptr, "STATUS"))
			supported_options |= UPS_STATUS;
		else if (!strcmp (ptr, "UPSTEMP"))
			supported_options |= UPS_TEMP;
	}

	return OK;
}


/* gets a variable value for a specific UPS  */
int
get_ups_variable (const char *varname, char *buf, size_t buflen)
{
	/*  char command[MAX_INPUT_BUFFER]; */
	char temp_buffer[MAX_INPUT_BUFFER];
	char send_buffer[MAX_INPUT_BUFFER];
	char *ptr;

	/* create the command string to send to the UPS daemon */
	if (ups_name)
		sprintf (send_buffer, "REQ %s@%s\n", varname, ups_name);
	else
		sprintf (send_buffer, "REQ %s\n", varname);

	/* send the command to the daemon and get a response back */
	if (process_tcp_request
			(server_address, server_port, send_buffer, temp_buffer,
			 sizeof (temp_buffer)) != STATE_OK) {
		printf ("Invalid response received from host\n");
		return ERROR;
	}

	if (ups_name)
		ptr = temp_buffer + strlen (varname) + 5 + strlen (ups_name) + 1;
	else
		ptr = temp_buffer + strlen (varname) + 5;

	if (!strcmp (ptr, "NOT-SUPPORTED")) {
		printf ("Error: Variable '%s' is not supported\n", varname);
		return ERROR;
	}

	if (!strcmp (ptr, "DATA-STALE")) {
		printf ("Error: UPS data is stale\n");
		return ERROR;
	}

	if (!strcmp (ptr, "UNKNOWN-UPS")) {
		if (ups_name)
			printf ("Error: UPS '%s' is unknown\n", ups_name);
		else
			printf ("Error: UPS is unknown\n");
		return ERROR;
	}

	strncpy (buf, ptr, buflen - 1);
	buf[buflen - 1] = 0;

	return OK;
}





/* Command line: CHECK_UPS <host_address> [-u ups] [-p port] [-v variable] 
			   [-wv warn_value] [-cv crit_value] [-to to_sec] */


/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"ups", required_argument, 0, 'u'},
		{"port", required_argument, 0, 'p'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"variable", required_argument, 0, 'v'},
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
		c = getopt_long (argc, argv, "hVH:u:p:v:c:w:t:", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage3 ("Unknown option", optopt);
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage2 ("Invalid host name", optarg);
			}
			break;
		case 'u':									/* ups name */
			ups_name = optarg;
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage2 ("Server port must be a positive integer", optarg);
			}
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_value = atoi (optarg);
				check_crit = TRUE;
			}
			else {
				usage2 ("Critical time must be a nonnegative integer", optarg);
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_value = atoi (optarg);
				check_warn = TRUE;
			}
			else {
				usage2 ("Warning time must be a nonnegative integer", optarg);
			}
			break;
		case 'v':									/* variable */
			if (!strcmp (optarg, "LINE"))
				check_variable = UPS_UTILITY;
			else if (!strcmp (optarg, "TEMP"))
				check_variable = UPS_TEMP;
			else if (!strcmp (optarg, "BATTPCT"))
				check_variable = UPS_BATTPCT;
			else if (!strcmp (optarg, "LOADPCT"))
				check_variable = UPS_LOADPCT;
			else
				usage2 ("Unrecognized UPS variable", optarg);
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
		}
	}


	if (server_address == NULL && argc > optind) {
		if (is_host (argv[optind]))
			server_address = argv[optind++];
		else
			usage ("Invalid host name");
	}

	if (server_address == NULL)
		server_address = strdup("127.0.0.1");

	return validate_arguments();
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
	asprintf (&myport, "%d", PORT);

	print_revision (progname, revision);

	printf (_("Copyright (c) 2000 Tom Shields"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("This plugin tests the UPS service on the specified host.\n\
Network UPS Tools from www.exploits.org must be running for this plugin to\n\
work.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_("\
 -u, --ups=STRING\n\
    Name of UPS\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf (_("\
This plugin attempts to determine the status of a UPS (Uninterruptible Power\n\
Supply) on a local or remote host. If the UPS is online or calibrating, the\n\
plugin will return an OK state. If the battery is on it will return a WARNING\n\
state.  If the UPS is off or has a low battery the plugin will return a CRITICAL\n\
state.\n\n"));

	printf (_("\
You may also specify a variable to check [such as temperature, utility voltage,\n\
battery load, etc.]  as well as warning and critical thresholds for the value of\n\
that variable.  If the remote host has multiple UPS that are being monitored you\n\
will have to use the [ups] option to specify which UPS to check.\n\n"));

	printf (_("Notes:\n\n\
This plugin requires that the UPSD daemon distributed with Russel Kroll's\n\
Smart UPS Tools be installed on the remote host.  If you do not have the\n\
package installed on your system, you can download it from\n\
http://www.exploits.org/nut\n\n"));

	printf (_(UT_SUPPORT));
}




void
print_usage (void)
{
	printf (_("\
Usage: %s -H host [-e expect] [-p port] [-w warn] [-c crit]\n\
    [-t timeout] [-v]\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
