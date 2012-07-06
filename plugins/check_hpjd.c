/*****************************************************************************
* 
* Nagios check_hpjd plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_hpjd plugin
* 
* This plugin tests the STATUS of an HP printer with a JetDirect card.
* Net-SNMP must be installed on the computer running the plugin.
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

const char *progname = "check_hpjd";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"
#include "netutils.h"

#define DEFAULT_COMMUNITY "public"


const char *option_summary = "-H host [-C community]\n";

#define HPJD_LINE_STATUS           ".1.3.6.1.4.1.11.2.3.9.1.1.2.1"
#define HPJD_PAPER_STATUS          ".1.3.6.1.4.1.11.2.3.9.1.1.2.2"
#define HPJD_INTERVENTION_REQUIRED ".1.3.6.1.4.1.11.2.3.9.1.1.2.3"
#define HPJD_GD_PERIPHERAL_ERROR   ".1.3.6.1.4.1.11.2.3.9.1.1.2.6"
#define HPJD_GD_PAPER_OUT          ".1.3.6.1.4.1.11.2.3.9.1.1.2.8"
#define HPJD_GD_PAPER_JAM          ".1.3.6.1.4.1.11.2.3.9.1.1.2.9"
#define HPJD_GD_TONER_LOW          ".1.3.6.1.4.1.11.2.3.9.1.1.2.10"
#define HPJD_GD_PAGE_PUNT          ".1.3.6.1.4.1.11.2.3.9.1.1.2.11"
#define HPJD_GD_MEMORY_OUT         ".1.3.6.1.4.1.11.2.3.9.1.1.2.12"
#define HPJD_GD_DOOR_OPEN          ".1.3.6.1.4.1.11.2.3.9.1.1.2.17"
#define HPJD_GD_PAPER_OUTPUT       ".1.3.6.1.4.1.11.2.3.9.1.1.2.19"
#define HPJD_GD_STATUS_DISPLAY     ".1.3.6.1.4.1.11.2.3.9.1.1.3"

#define ONLINE		0
#define OFFLINE		1

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char *community = NULL;
char *address = NULL;

int
main (int argc, char **argv)
{
	char command_line[1024];
	int result = STATE_UNKNOWN;
	int line;
	char input_buffer[MAX_INPUT_BUFFER];
	char query_string[512];
	char *errmsg;
	char *temp_buffer;
	int line_status = ONLINE;
	int paper_status = 0;
	int intervention_required = 0;
	int peripheral_error = 0;
	int paper_jam = 0;
	int paper_out = 0;
	int toner_low = 0;
	int page_punt = 0;
	int memory_out = 0;
	int door_open = 0;
	int paper_output = 0;
	char display_message[MAX_INPUT_BUFFER];

	errmsg = malloc(MAX_INPUT_BUFFER);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* removed ' 2>1' at end of command 10/27/1999 - EG */
	/* create the query string */
	sprintf
		(query_string,
		 "%s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0",
		 HPJD_LINE_STATUS,
		 HPJD_PAPER_STATUS,
		 HPJD_INTERVENTION_REQUIRED,
		 HPJD_GD_PERIPHERAL_ERROR,
		 HPJD_GD_PAPER_JAM,
		 HPJD_GD_PAPER_OUT,
		 HPJD_GD_TONER_LOW,
		 HPJD_GD_PAGE_PUNT,
		 HPJD_GD_MEMORY_OUT,
		 HPJD_GD_DOOR_OPEN, HPJD_GD_PAPER_OUTPUT, HPJD_GD_STATUS_DISPLAY);

	/* get the command to run */
	sprintf (command_line, "%s -OQa -m : -v 1 -c %s %s %s", PATH_TO_SNMPGET, community,
									address, query_string);

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), command_line);
	}

	result = STATE_OK;

	line = 0;
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		/* strip the newline character from the end of the input */
		if (input_buffer[strlen (input_buffer) - 1] == '\n')
			input_buffer[strlen (input_buffer) - 1] = 0;

		line++;

		temp_buffer = strtok (input_buffer, "=");
		temp_buffer = strtok (NULL, "=");

		if (temp_buffer == NULL && line < 13) {

				result = STATE_UNKNOWN;
				strcpy (errmsg, input_buffer);

		} else {

			switch (line) {

			case 1:										/* 1st line should contain the line status */
				line_status = atoi (temp_buffer);
				break;
			case 2:										/* 2nd line should contain the paper status */
				paper_status = atoi (temp_buffer);
				break;
			case 3:										/* 3rd line should be intervention required */
				intervention_required = atoi (temp_buffer);
				break;
			case 4:										/* 4th line should be peripheral error */
				peripheral_error = atoi (temp_buffer);
				break;
			case 5:										/* 5th line should contain the paper jam status */
				paper_jam = atoi (temp_buffer);
				break;
			case 6:										/* 6th line should contain the paper out status */
				paper_out = atoi (temp_buffer);
				break;
			case 7:										/* 7th line should contain the toner low status */
				toner_low = atoi (temp_buffer);
				break;
			case 8:										/* did data come too slow for engine */
				page_punt = atoi (temp_buffer);
				break;
			case 9:										/* did we run out of memory */
				memory_out = atoi (temp_buffer);
				break;
			case 10:										/* is there a door open */
				door_open = atoi (temp_buffer);
				break;
			case 11:										/* is output tray full */
				paper_output = atoi (temp_buffer);
				break;
			case 12:										/* display panel message */
				strcpy (display_message, temp_buffer + 1);
				break;
			default:										/* fold multiline message */
				strncat (display_message, input_buffer,
						sizeof (display_message) - strlen (display_message) - 1);
			}

		}

		/* break out of the read loop if we encounter an error */
		if (result != STATE_OK)
			break;
	}

	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		result = max_state (result, STATE_WARNING);
		/* remove CRLF */
		if (input_buffer[strlen (input_buffer) - 1] == '\n')
			input_buffer[strlen (input_buffer) - 1] = 0;
		sprintf (errmsg, "%s", input_buffer );

	}

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	/* if there wasn't any output, display an error */
	if (line == 0) {

		/* might not be the problem, but most likely is. */
		result = STATE_UNKNOWN ;
		xasprintf (&errmsg, "%s : Timeout from host %s\n", errmsg, address );

	}

	/* if we had no read errors, check the printer status results... */
	if (result == STATE_OK) {

		if (paper_jam) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Paper Jam"));
		}
		else if (paper_out) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Out of Paper"));
		}
		else if (line_status == OFFLINE) {
			if (strcmp (errmsg, "POWERSAVE ON") != 0) {
				result = STATE_WARNING;
				strcpy (errmsg, _("Printer Offline"));
			}
		}
		else if (peripheral_error) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Peripheral Error"));
		}
		else if (intervention_required) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Intervention Required"));
		}
		else if (toner_low) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Toner Low"));
		}
		else if (memory_out) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Insufficient Memory"));
		}
		else if (door_open) {
			result = STATE_WARNING;
			strcpy (errmsg, _("A Door is Open"));
		}
		else if (paper_output) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Output Tray is Full"));
		}
		else if (page_punt) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Data too Slow for Engine"));
		}
		else if (paper_status) {
			result = STATE_WARNING;
			strcpy (errmsg, _("Unknown Paper Error"));
		}
	}

	if (result == STATE_OK)
		printf (_("Printer ok - (%s)\n"), display_message);

	else if (result == STATE_UNKNOWN) {

		printf ("%s\n", errmsg);

		/* if printer could not be reached, escalate to critical */
		if (strstr (errmsg, "Timeout"))
			result = STATE_CRITICAL;
	}

	else if (result == STATE_WARNING)
		printf ("%s (%s)\n", errmsg, display_message);

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
		{"community", required_argument, 0, 'C'},
/*  		{"critical",       required_argument,0,'c'}, */
/*  		{"warning",        required_argument,0,'w'}, */
/*  		{"port",           required_argument,0,'P'}, */
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;


	while (1) {
		c = getopt_long (argc, argv, "+hVH:C:", longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				address = strscpy(address, optarg) ;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'C':									/* community */
			community = strscpy (community, optarg);
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;
	if (address == NULL) {
		if (is_host (argv[c])) {
			address = argv[c++];
		}
		else {
			usage2 (_("Invalid hostname/address"), argv[c]);
		}
	}

	if (community == NULL) {
		if (argv[c] != NULL )
			community = argv[c];
		else
			community = strdup (DEFAULT_COMMUNITY);
	}

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
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin tests the STATUS of an HP printer with a JetDirect card."));
	printf ("%s\n", _("Net-snmp must be installed on the computer running the plugin."));

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-C, --community=STRING");
	printf ("    %s", _("The SNMP community name "));
	printf (_("(default=%s)"), DEFAULT_COMMUNITY);
	printf ("\n");

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -H host [-C community]\n", progname);
}
