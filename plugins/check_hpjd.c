/******************************************************************************
*
* CHECK_HPJD.C
*
* Program: HP printer plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Command line: CHECK_HPJD <ip_address> [community]
*
* Description:
*
* This plugin will attempt to check the status of an HP printer.  The
* printer must have a JetDirect card installed and TCP/IP protocol
* stack enabled.  This plugin has only been tested on a few printers
* and may not work well on all models of JetDirect cards.  Multiple
* port JetDirect devices must have an IP address assigned to each
* port in order to be monitored.
*
* Dependencies:
*
* This plugin used the 'snmpget' command included with the UCD-SNMP
* package.  If you don't have the package installed you will need to
* download it from http://ucd-snmp.ucdavis.edu before you can use
* this plugin.
*
* Return Values:
*
* UNKNOWN	= The plugin could not read/process the output from the printer
* OK		= Printer looks normal
* WARNING	= Low toner, paper jam, intervention required, paper out, etc.
* CRITICAL	= The printer could not be reached (it's probably turned off)
*
* Acknowledgements:
*
* The idea for the plugin (as well as some code) were taken from Jim
* Trocki's pinter alert script in his "mon" utility, found at
* http://www.kernel.org/software/mon
*
* Notes:
* 'JetDirect' is copyrighted by Hewlett-Packard.  
*                    HP, please don't sue me... :-)
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

#include "common.h"
#include "popen.h"
#include "utils.h"

const char *progname = "check_hpjd";
#define REVISION "$Revision$"
#define COPYRIGHT "2000-2002"

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
	sprintf (command_line, "%s -m : -v 1 %s -c %s %s", PATH_TO_SNMPGET, address,
					 community, query_string);

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
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	/* if there wasn't any output, display an error */
	if (line == 0) {

		/*
		   result=STATE_UNKNOWN;
		   strcpy(error_message,"Error: Could not read plugin output\n");
		 */

		/* might not be the problem, but most likely is.. */
		result = STATE_UNKNOWN;
		sprintf (error_message, "Timeout: No response from %s\n", address);
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

#ifdef HAVE_GETOPT_H
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
#endif

	if (argc < 2)
		return ERROR;

	
	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "+hVH:C:", long_options, &option_index);
#else
		c = getopt (argc, argv, "+?hVH:C:");
#endif

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
			print_revision (progname, REVISION);
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
	print_revision (progname, REVISION);
	printf
		("Copyright (c) 2000 Ethan Galstad/Karl DeBisschop\n\n"
		 "This plugin tests the STATUS of an HP printer with a JetDirect card.\n"
		 "Net-snmp must be installed on the computer running the plugin.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -H, --hostname=STRING or IPADDRESS\n"
		 "   Check server on the indicated host\n"
		 " -C, --community=STRING\n"
		 "   The SNMP community name (default=%s)\n"
		 " -h, --help\n"
		 "   Print detailed help screen\n"
		 " -V, --version\n" "   Print version information\n\n",DEFAULT_COMMUNITY);
	support ();
}





void
print_usage (void)
{
	printf
		("Usage: %s -H host [-C community]\n"
		 "       %s --help\n"
		 "       %s --version\n", progname, progname, progname);
}


/*
	if(argc<2||argc>3){
	printf("Incorrect number of arguments supplied\n");
	printf("\n");
	print_revision(argv[0],"$Revision$");
	printf("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n");
	printf("License: GPL\n");
	printf("\n");
	printf("Usage: %s <ip_address> [community]\n",argv[0]);
	printf("\n");
	printf("Note:\n");
	printf(" <ip_address>     = The IP address of the JetDirect card\n");
	printf(" [community]      = An optional community string used for SNMP communication\n");
	printf("                    with the JetDirect card.  The default is 'public'.\n");
	printf("\n");
	return STATE_UNKNOWN;
	}

	/* get the IP address of the JetDirect device */
	strcpy(address,argv[1]);
	
	/* get the community name to use for SNMP communication */
	if(argc>=3)
	strcpy(community,argv[2]);
	else
	strcpy(community,"public");
*/
