/******************************************************************************
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

#include "common.h"
#include "popen.h"
#include "utils.h"

const char *progname = "check_hpjd";
const char *revision = "$Revision$";
const char *authors = "Nagios Plugin Development Team";
const char *email = "nagiosplug-devel@lists.sourceforge.net";
const char *copyright = "2000-2003";

const char *summary = "\
This plugin tests the STATUS of an HP printer with a JetDirect card.\n\
Net-snmp must be installed on the computer running the plugin.\n\n";

const char *option_summary = "-H host [-C community]\n";

const char *options = "\
 -H, --hostname=STRING or IPADDRESS\n\
   Check server on the indicated host\n\
 -C, --community=STRING\n\
   The SNMP community name (default=%s)\n\
 -h, --help\n\
   Print detailed help screen\n\
 -V, --version\n\
   Print version information\n\n";

#define HPJD_LINE_STATUS		".1.3.6.1.4.1.11.2.3.9.1.1.2.1"
#define HPJD_PAPER_STATUS		".1.3.6.1.4.1.11.2.3.9.1.1.2.2"
#define HPJD_INTERVENTION_REQUIRED	".1.3.6.1.4.1.11.2.3.9.1.1.2.3"
#define HPJD_GD_PERIPHERAL_ERROR	".1.3.6.1.4.1.11.2.3.9.1.1.2.6"
#define HPJD_GD_PAPER_JAM		".1.3.6.1.4.1.11.2.3.9.1.1.2.8"
#define HPJD_GD_PAPER_OUT		".1.3.6.1.4.1.11.2.3.9.1.1.2.9"
#define HPJD_GD_TONER_LOW		".1.3.6.1.4.1.11.2.3.9.1.1.2.10"
#define HPJD_GD_PAGE_PUNT		".1.3.6.1.4.1.11.2.3.9.1.1.2.11"
#define HPJD_GD_MEMORY_OUT		".1.3.6.1.4.1.11.2.3.9.1.1.2.12"
#define HPJD_GD_DOOR_OPEN	 	".1.3.6.1.4.1.11.2.3.9.1.1.2.17"
#define HPJD_GD_PAPER_OUTPUT		".1.3.6.1.4.1.11.2.3.9.1.1.2.19"
#define HPJD_GD_STATUS_DISPLAY		".1.3.6.1.4.1.11.2.3.9.1.1.3"

#define ONLINE		0
#define OFFLINE		1

#define DEFAULT_COMMUNITY "public"

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

char *community = DEFAULT_COMMUNITY;
char *address = NULL;


int
main (int argc, char **argv)
{
	char command_line[1024];
	int result;
	int line;
	char input_buffer[MAX_INPUT_BUFFER];
	char query_string[512];
	char error_message[MAX_INPUT_BUFFER];
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
	char *temp ;

	if (process_arguments (argc, argv) != OK)
		usage ("Invalid command arguments supplied\n");

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
	sprintf (command_line, "%s -m : -v 1 -c %s %s %s", PATH_TO_SNMPGET, community, 
									address, query_string);

	/* run the command */
	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", command_line);
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

		switch (line) {

		case 1:										/* 1st line should contain the line status */
			if (temp_buffer != NULL)
				line_status = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 2:										/* 2nd line should contain the paper status */
			if (temp_buffer != NULL)
				paper_status = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 3:										/* 3rd line should be intervention required */
			if (temp_buffer != NULL)
				intervention_required = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 4:										/* 4th line should be peripheral error */
			if (temp_buffer != NULL)
				peripheral_error = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 5:										/* 5th line should contain the paper jam status */
			if (temp_buffer != NULL)
				paper_jam = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 6:										/* 6th line should contain the paper out status */
			if (temp_buffer != NULL)
				paper_out = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 7:										/* 7th line should contain the toner low status */
			if (temp_buffer != NULL)
				toner_low = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 8:										/* did data come too slow for engine */
			if (temp_buffer != NULL)
				page_punt = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 9:										/* did we run out of memory */
			if (temp_buffer != NULL)
				memory_out = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 10:										/* is there a door open */
			if (temp_buffer != NULL)
				door_open = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 11:										/* is output tray full */
			if (temp_buffer != NULL)
				paper_output = atoi (temp_buffer);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		case 12:										/* display panel message */
			if (temp_buffer != NULL)
				strcpy (display_message, temp_buffer + 1);
			else {
				result = STATE_UNKNOWN;
				strcpy (error_message, input_buffer);
			}
			break;

		default:
			break;
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
		sprintf (error_message, "%s", input_buffer );

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
		asprintf (&temp, error_message);
		sprintf (error_message, "%s : Timeout from host %s\n", temp, address );
		 
	}

	/* if we had no read errors, check the printer status results... */
	if (result == STATE_OK) {

		if (paper_jam) {
			result = STATE_WARNING;
			strcpy (error_message, "Paper Jam");
		}
		else if (paper_out) {
			result = STATE_WARNING;
			strcpy (error_message, "Out of Paper");
		}
		else if (line_status == OFFLINE) {
			if (strcmp (error_message, "POWERSAVE ON") != 0) {
				result = STATE_WARNING;
				strcpy (error_message, "Printer Offline");
			}
		}
		else if (peripheral_error) {
			result = STATE_WARNING;
			strcpy (error_message, "Peripheral Error");
		}
		else if (intervention_required) {
			result = STATE_WARNING;
			strcpy (error_message, "Intervention Required");
		}
		else if (toner_low) {
			result = STATE_WARNING;
			strcpy (error_message, "Toner Low");
		}
		else if (memory_out) {
			result = STATE_WARNING;
			strcpy (error_message, "Insufficient Memory");
		}
		else if (door_open) {
			result = STATE_WARNING;
			strcpy (error_message, "A Door is Open");
		}
		else if (paper_output) {
			result = STATE_WARNING;
			strcpy (error_message, "Output Tray is Full");
		}
		else if (page_punt) {
			result = STATE_WARNING;
			strcpy (error_message, "Data too Slow for Engine");
		}
		else if (paper_status) {
			result = STATE_WARNING;
			strcpy (error_message, "Unknown Paper Error");
		}
	}

	if (result == STATE_OK)
		printf ("Printer ok - (%s)\n", display_message);

	else if (result == STATE_UNKNOWN) {

		printf ("%s\n", error_message);

		/* if printer could not be reached, escalate to critical */
		if (strstr (error_message, "Timeout"))
			result = STATE_CRITICAL;
	}

	else if (result == STATE_WARNING)
		printf ("%s (%s)\n", error_message, display_message);

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
		c = getopt_long (argc, argv, "+hVH:C:", long_options, &option_index);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				address = strscpy(address, optarg) ;
			}
			else {
				usage ("Invalid host name\n");
			}
			break;
		case 'C':									/* community */
			community = strscpy (community, optarg);
			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("Invalid argument\n");
		}
	}

	c = optind;
	if (address == NULL) {
		if (is_host (argv[c])) {
			address = argv[c++];
		}
		else {
			usage ("Invalid host name");
		}
	}
	
	if (argv[c] != NULL ) {
		community = argv[c];
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
	print_revision (progname, revision);
	printf ("Copyright (c) %s %s\n\t<%s>\n\n", copyright, authors, email);
	printf (summary);
	print_usage ();
	printf ("\nOptions:\n");
	printf (options, DEFAULT_COMMUNITY);
	support ();
}

void
print_usage (void)
{
	printf ("\
Usage:\n\
 %s %s\n\
 %s (-h | --help) for detailed help\n\
 %s (-V | --version) for version information\n",
	        progname, option_summary, progname, progname);
}



