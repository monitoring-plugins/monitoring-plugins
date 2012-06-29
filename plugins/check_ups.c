/*****************************************************************************
* 
* Nagios check_ups plugin
* 
* License: GPL
* Copyright (c) 2000 Tom Shields
*               2004 Alain Richard <alain.richard@equation.fr>
*               2004 Arnaud Quette <arnaud.quette@mgeups.com>
* Copyright (c) 2002-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains Network UPS Tools plugin for Nagios
* 
* This plugin tests the UPS service on the specified host. Network UPS Tools
* from www.networkupstools.org must be running for this plugin to work.
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

const char *progname = "check_ups";
const char *copyright = "2000-2007";
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

#define UPSSTATUS_NONE       0
#define UPSSTATUS_OFF        1
#define UPSSTATUS_OL         2
#define UPSSTATUS_OB         4
#define UPSSTATUS_LB         8
#define UPSSTATUS_CAL       16
#define UPSSTATUS_RB        32  /*Replace Battery */
#define UPSSTATUS_BYPASS    64
#define UPSSTATUS_OVER     128
#define UPSSTATUS_TRIM     256
#define UPSSTATUS_BOOST    512
#define UPSSTATUS_CHRG    1024
#define UPSSTATUS_DISCHRG 2048
#define UPSSTATUS_UNKOWN  4096

enum { NOSUCHVAR = ERROR-1 };

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
int temp_output_c = 0;

int determine_status (void);
int get_ups_variable (const char *, char *, size_t);

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	char *message;
	char *data;
	char *tunits;
	char temp_buffer[MAX_INPUT_BUFFER];
	double ups_utility_deviation = 0.0;
	int res;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	ups_status = strdup ("N/A");
	data = strdup ("");
	message = strdup ("");

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* get the ups status if possible */
	if (determine_status () != OK)
		return STATE_CRITICAL;
	if (supported_options & UPS_STATUS) {

		ups_status = strdup ("");
		result = STATE_OK;

		if (status & UPSSTATUS_OFF) {
			xasprintf (&ups_status, "Off");
			result = STATE_CRITICAL;
		}
		else if ((status & (UPSSTATUS_OB | UPSSTATUS_LB)) ==
						 (UPSSTATUS_OB | UPSSTATUS_LB)) {
			xasprintf (&ups_status, _("On Battery, Low Battery"));
			result = STATE_CRITICAL;
		}
		else {
			if (status & UPSSTATUS_OL) {
				xasprintf (&ups_status, "%s%s", ups_status, _("Online"));
			}
			if (status & UPSSTATUS_OB) {
				xasprintf (&ups_status, "%s%s", ups_status, _("On Battery"));
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_LB) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Low Battery"));
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_CAL) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Calibrating"));
			}
			if (status & UPSSTATUS_RB) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Replace Battery"));
				result = STATE_WARNING;
			}
			if (status & UPSSTATUS_BYPASS) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", On Bypass"));
			}
			if (status & UPSSTATUS_OVER) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Overload"));
			}
			if (status & UPSSTATUS_TRIM) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Trimming"));
			}
			if (status & UPSSTATUS_BOOST) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Boosting"));
			}
			if (status & UPSSTATUS_CHRG) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Charging"));
			}
			if (status & UPSSTATUS_DISCHRG) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Discharging"));
			}
			if (status & UPSSTATUS_UNKOWN) {
				xasprintf (&ups_status, "%s%s", ups_status, _(", Unknown"));
			}
		}
		xasprintf (&message, "%sStatus=%s ", message, ups_status);
	}

	/* get the ups utility voltage if possible */
	res=get_ups_variable ("input.voltage", temp_buffer, sizeof (temp_buffer));
	if (res == NOSUCHVAR) supported_options &= ~UPS_UTILITY;
	else if (res != OK)
		return STATE_CRITICAL;
	else {
		supported_options |= UPS_UTILITY;

		ups_utility_voltage = atof (temp_buffer);
		xasprintf (&message, "%sUtility=%3.1fV ", message, ups_utility_voltage);

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
			xasprintf (&data, "%s",
			          perfdata ("voltage", (long)(1000*ups_utility_voltage), "mV",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, FALSE, 0));
		} else {
			xasprintf (&data, "%s",
			          perfdata ("voltage", (long)(1000*ups_utility_voltage), "mV",
			                    FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
		}
	}

	/* get the ups battery percent if possible */
	res=get_ups_variable ("battery.charge", temp_buffer, sizeof (temp_buffer));
	if (res == NOSUCHVAR) supported_options &= ~UPS_BATTPCT;
	else if ( res != OK)
		return STATE_CRITICAL;
	else {
		supported_options |= UPS_BATTPCT;
		ups_battery_percent = atof (temp_buffer);
		xasprintf (&message, "%sBatt=%3.1f%% ", message, ups_battery_percent);

		if (check_variable == UPS_BATTPCT) {
			if (check_crit==TRUE && ups_battery_percent <= critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn==TRUE && ups_battery_percent<=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			xasprintf (&data, "%s %s", data,
			          perfdata ("battery", (long)ups_battery_percent, "%",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, TRUE, 100));
		} else {
			xasprintf (&data, "%s %s", data,
			          perfdata ("battery", (long)ups_battery_percent, "%",
			                    FALSE, 0, FALSE, 0, TRUE, 0, TRUE, 100));
		}
	}

	/* get the ups load percent if possible */
	res=get_ups_variable ("ups.load", temp_buffer, sizeof (temp_buffer));
	if ( res == NOSUCHVAR ) supported_options &= ~UPS_LOADPCT;
	else if ( res != OK)
		return STATE_CRITICAL;
	else {
		supported_options |= UPS_LOADPCT;
		ups_load_percent = atof (temp_buffer);
		xasprintf (&message, "%sLoad=%3.1f%% ", message, ups_load_percent);

		if (check_variable == UPS_LOADPCT) {
			if (check_crit==TRUE && ups_load_percent>=critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn==TRUE && ups_load_percent>=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			xasprintf (&data, "%s %s", data,
			          perfdata ("load", (long)ups_load_percent, "%",
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, TRUE, 100));
		} else {
			xasprintf (&data, "%s %s", data,
			          perfdata ("load", (long)ups_load_percent, "%",
			                    FALSE, 0, FALSE, 0, TRUE, 0, TRUE, 100));
		}
	}

	/* get the ups temperature if possible */
	res=get_ups_variable ("ups.temperature", temp_buffer, sizeof (temp_buffer));
	if ( res == NOSUCHVAR ) supported_options &= ~UPS_TEMP;
	else if ( res != OK)
		return STATE_CRITICAL;
	else {
 		supported_options |= UPS_TEMP;
		if (temp_output_c) {
		  tunits="degC";
		  ups_temperature = atof (temp_buffer);
		  xasprintf (&message, "%sTemp=%3.1fC", message, ups_temperature);
		}
		else {
		  tunits="degF";
		  ups_temperature = (atof (temp_buffer) * 1.8) + 32;
		  xasprintf (&message, "%sTemp=%3.1fF", message, ups_temperature);
		}

		if (check_variable == UPS_TEMP) {
			if (check_crit==TRUE && ups_temperature>=critical_value) {
				result = STATE_CRITICAL;
			}
			else if (check_warn == TRUE && ups_temperature>=warning_value) {
				result = max_state (result, STATE_WARNING);
			}
			xasprintf (&data, "%s %s", data,
			          perfdata ("temp", (long)ups_temperature, tunits,
			                    check_warn, (long)(1000*warning_value),
			                    check_crit, (long)(1000*critical_value),
			                    TRUE, 0, FALSE, 0));
		} else {
			xasprintf (&data, "%s %s", data,
			          perfdata ("temp", (long)ups_temperature, tunits,
			                    FALSE, 0, FALSE, 0, TRUE, 0, FALSE, 0));
		}
	}

	/* if the UPS does not support any options we are looking for, report an error */
	if (supported_options == UPS_NONE) {
		result = STATE_CRITICAL;
		xasprintf (&message, _("UPS does not support any available options\n"));
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
	int res;

	res=get_ups_variable ("ups.status", recv_buffer, sizeof (recv_buffer));
	if (res == NOSUCHVAR) return OK;
	if (res != STATE_OK) {
		printf ("%s\n", _("Invalid response received from host"));
		return ERROR;
	}

	supported_options |= UPS_STATUS;

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
		else if (!strcmp (ptr, "BYPASS"))
			status |= UPSSTATUS_BYPASS;
		else if (!strcmp (ptr, "OVER"))
			status |= UPSSTATUS_OVER;
		else if (!strcmp (ptr, "TRIM"))
			status |= UPSSTATUS_TRIM;
		else if (!strcmp (ptr, "BOOST"))
			status |= UPSSTATUS_BOOST;
		else if (!strcmp (ptr, "CHRG"))
			status |= UPSSTATUS_CHRG;
		else if (!strcmp (ptr, "DISCHRG"))
			status |= UPSSTATUS_DISCHRG;
		else
			status |= UPSSTATUS_UNKOWN;
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
	char *logout = "OK Goodbye\n";
	int logout_len = strlen(logout);
	int len;

	*buf=0;

	/* create the command string to send to the UPS daemon */
	/* Add LOGOUT to avoid read failure logs */
	sprintf (send_buffer, "GET VAR %s %s\nLOGOUT\n", ups_name, varname);

	/* send the command to the daemon and get a response back */
	if (process_tcp_request
			(server_address, server_port, send_buffer, temp_buffer,
			 sizeof (temp_buffer)) != STATE_OK) {
		printf ("%s\n", _("Invalid response received from host"));
		return ERROR;
	}

	ptr = temp_buffer;
	len = strlen(ptr);
	if (len > logout_len && strcmp (ptr + len - logout_len, logout) == 0) len -= logout_len;
	if (len > 0 && ptr[len-1] == '\n') ptr[len-1]=0;
	if (strcmp (ptr, "ERR UNKNOWN-UPS") == 0) {
		printf (_("CRITICAL - no such UPS '%s' on that host\n"), ups_name);
		return ERROR;
	}

	if (strcmp (ptr, "ERR VAR-NOT-SUPPORTED") == 0) {
		/*printf ("Error: Variable '%s' is not supported\n", varname);*/
		return NOSUCHVAR;
	}

	if (strcmp (ptr, "ERR DATA-STALE") == 0) {
		printf ("%s\n", _("CRITICAL - UPS data is stale"));
		return ERROR;
	}

	if (strncmp (ptr, "ERR", 3) == 0) {
		printf (_("Unknown error: %s\n"), ptr);
		return ERROR;
	}

	ptr = temp_buffer + strlen (varname) + strlen (ups_name) + 6;
	len = strlen(ptr);
	if (len < 2 || ptr[0] != '"' || ptr[len-1] != '"') {
		printf ("%s\n", _("Error: unable to parse variable"));
		return ERROR;
	}
	strncpy (buf, ptr+1, len - 2);
	buf[len - 2] = 0;

	return OK;
}


/* Command line: CHECK_UPS -H <host_address> -u ups [-p port] [-v variable]
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
		{"temperature", no_argument, 0, 'T'},
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
		c = getopt_long (argc, argv, "hVTH:u:p:v:c:w:t:", longopts,
									 &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage5 ();
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'T': /* FIXME: to be improved (ie "-T C" for Celsius or "-T F" for Farenheit) */
			temp_output_c = 1;
			break;
		case 'u':									/* ups name */
			ups_name = optarg;
			break;
		case 'p':									/* port */
			if (is_intpos (optarg)) {
				server_port = atoi (optarg);
			}
			else {
				usage2 (_("Port must be a positive integer"), optarg);
			}
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_value = atoi (optarg);
				check_crit = TRUE;
			}
			else {
				usage2 (_("Critical time must be a positive integer"), optarg);
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_value = atoi (optarg);
				check_warn = TRUE;
			}
			else {
				usage2 (_("Warning time must be a positive integer"), optarg);
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
				usage2 (_("Unrecognized UPS variable"), optarg);
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
		}
	}


	if (server_address == NULL && argc > optind) {
		if (is_host (argv[optind]))
			server_address = argv[optind++];
		else
			usage2 (_("Invalid hostname/address"), optarg);
	}

	if (server_address == NULL)
		server_address = strdup("127.0.0.1");

	return validate_arguments();
}


int
validate_arguments (void)
{
	if (! ups_name) {
		printf ("%s\n", _("Error : no UPS indicated"));
		return ERROR;
	}
	return OK;
}


void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 2000 Tom Shields\n");
	printf ("Copyright (c) 2004 Alain Richard <alain.richard@equation.fr>\n");
	printf ("Copyright (c) 2004 Arnaud Quette <arnaud.quette@mgeups.com>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin tests the UPS service on the specified host. Network UPS Tools"));
  printf ("%s\n", _("from www.networkupstools.org must be running for this plugin to work."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (" %s\n", "-u, --ups=STRING");
  printf ("    %s\n", _("Name of UPS"));
  printf (" %s\n", "-T, --temperature");
  printf ("    %s\n", _("Output of temperatures in Celsius"));
  printf (" %s\n", "-v, --variable=STRING");
  printf ("    %s %s\n", _("Valid values for STRING are"), "LINE, TEMP, BATTPCT or LOADPCT");

	printf (UT_WARN_CRIT);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

/* TODO: -v clashing with -v/-variable. Commenting out help text since verbose
         is unused up to now */
/*	printf (UT_VERBOSE); */

  printf ("\n");
	printf ("%s\n", _("This plugin attempts to determine the status of a UPS (Uninterruptible Power"));
  printf ("%s\n", _("Supply) on a local or remote host. If the UPS is online or calibrating, the"));
  printf ("%s\n", _("plugin will return an OK state. If the battery is on it will return a WARNING"));
  printf ("%s\n", _("state. If the UPS is off or has a low battery the plugin will return a CRITICAL"));
  printf ("%s\n", _("state."));

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("You may also specify a variable to check (such as temperature, utility voltage,"));
  printf (" %s\n", _("battery load, etc.) as well as warning and critical thresholds for the value"));
  printf (" %s\n", _("of that variable.  If the remote host has multiple UPS that are being monitored"));
  printf (" %s\n", _("you will have to use the --ups option to specify which UPS to check."));
  printf ("\n");
  printf (" %s\n", _("This plugin requires that the UPSD daemon distributed with Russell Kroll's"));
  printf (" %s\n", _("Network UPS Tools be installed on the remote host. If you do not have the"));
  printf (" %s\n", _("package installed on your system, you can download it from"));
  printf (" %s\n", _("http://www.networkupstools.org"));

	printf (UT_SUPPORT);
}


void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host -u ups [-p port] [-v variable] [-w warn_value] [-c crit_value] [-to to_sec] [-T]\n", progname);
}
