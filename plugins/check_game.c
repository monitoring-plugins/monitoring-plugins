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

const char *progname = "check_game";
const char *revision = "$Revision$";
const char *copyright = "2002-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

void
print_usage (void)
{
	printf (_("\
Usage: %s <game> <ip_address> [-p port] [-gf game_field] [-mf map_field]\n\
  [-pf ping_field]\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}

void
print_help (void)
{
	print_revision (progname, revision);

	printf (_(COPYRIGHT), copyright, email);

	printf (_("This plugin tests %s connections with the specified host."), progname);

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
<game>        = Game type that is recognised by qstat (without the leading dash)\n\
<ip_address>  = The IP address of the device you wish to query\n\
 [port]        = Optional port of which to connect\n\
 [game_field]  = Field number in raw qstat output that contains game name\n\
 [map_field]   = Field number in raw qstat output that contains map name\n\
 [ping_field]  = Field number in raw qstat output that contains ping time\n"),
	        DEFAULT_SOCKET_TIMEOUT);

	printf (_("\n\
Notes:\n\
- This plugin uses the 'qstat' command, the popular game server status query tool .\n\
  If you don't have the package installed, you will need to download it from\n\
  http://www.activesw.com/people/steve/qstat.html before you can use this plugin.\n"));

	printf (_(UT_SUPPORT));
}

int process_arguments (int, char **);
int validate_arguments (void);

#define QSTAT_DATA_DELIMITER 	","

#define QSTAT_HOST_ERROR	"ERROR"
#define QSTAT_HOST_DOWN		"DOWN"
#define QSTAT_HOST_TIMEOUT	"TIMEOUT"
#define QSTAT_MAX_RETURN_ARGS	12

char *server_ip;
char *game_type;
int port = 0;

int verbose;

int qstat_game_players_max = 4;
int qstat_game_players = 5;
int qstat_game_field = 2;
int qstat_map_field = 3;
int qstat_ping_field = 5;


int
main (int argc, char **argv)
{
	char *command_line;
	int result;
	FILE *fp;
	char input_buffer[MAX_INPUT_BUFFER];
	char *p, *ret[QSTAT_MAX_RETURN_ARGS];
	int i;

	result = process_arguments (argc, argv);

	if (result != OK) {
		printf (_("Incorrect arguments supplied\n"));
		printf ("\n");
		print_revision (progname, revision);
		printf (_("Copyright (c) 1999 Ian Cass, Knowledge Matters Limited\n"));
		printf (_("License: GPL\n"));
		printf ("\n");
		return STATE_UNKNOWN;
	}

	result = STATE_OK;

	/* create the command line to execute */
	asprintf (&command_line, "%s -raw %s -%s %s",
						PATH_TO_QSTAT, QSTAT_DATA_DELIMITER, game_type, server_ip);
	
	if (port)
		asprintf (&command_line, "%s:%-d", command_line, port);

	if (verbose > 0)
		printf ("%s\n", command_line);

	/* run the command */
	fp = spopen (command_line);
	if (fp == NULL) {
		printf (_("Error - Could not open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp);	/* Only interested in the first line */

	/* strip the newline character from the end of the input */
	input_buffer[strlen (input_buffer) - 1] = 0;

	/* sanity check */
	/* was thinking about running qstat without any options, capturing the
	   -default line, parsing it & making an array of all know server types
	   but thought this would be too much hassle considering this is a tool
	   for intelligent sysadmins (ha). Could put a static array of known 
	   server types in a header file but then we'd be limiting ourselves

	   In the end, I figured I'd simply let an error occur & then trap it
	 */

	if (!strncmp (input_buffer, "unknown option", 14)) {
		printf (_("ERROR: Host type parameter incorrect!\n"));
		result = STATE_CRITICAL;
		return result;
	}

	/* initialize the returned data buffer */
	for (i = 0; i < QSTAT_MAX_RETURN_ARGS; i++)
		ret[i] = "";

	i = 0;
	p = (char *) strtok (input_buffer, QSTAT_DATA_DELIMITER);
	while (p != NULL) {
		ret[i] = p;
		p = (char *) strtok (NULL, QSTAT_DATA_DELIMITER);
		i++;
		if (i >= QSTAT_MAX_RETURN_ARGS)
			break;
	}

	if (strstr (ret[2], QSTAT_HOST_ERROR)) {
		printf ("ERROR: Host not found\n");
		result = STATE_CRITICAL;
	}
	else if (strstr (ret[2], QSTAT_HOST_DOWN)) {
		printf ("ERROR: Game server down or unavailable\n");
		result = STATE_CRITICAL;
	}
	else if (strstr (ret[2], QSTAT_HOST_TIMEOUT)) {
		printf ("ERROR: Game server timeout\n");
		result = STATE_CRITICAL;
	}
	else {
		printf ("OK: %s/%s %s (%s), Ping: %s ms\n", 
		        ret[qstat_game_players],
		        ret[qstat_game_players_max],
		        ret[qstat_game_field], 
		        ret[qstat_map_field],
		        ret[qstat_ping_field]);
	}

	/* close the pipe */
	spclose (fp);

	return result;
}



int
process_arguments (int argc, char **argv)
{
	int c;

	int opt_index = 0;
	static struct option long_opts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'P'},
		{"game-type", required_argument, 0, 'G'},
		{"map-field", required_argument, 0, 'm'},
		{"ping-field", required_argument, 0, 'p'},
		{"game-field", required_argument, 0, 'g'},
		{"players-field", required_argument, 0, 129},
		{"max-players-field", required_argument, 0, 130},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-mf", argv[c]) == 0)
			strcpy (argv[c], "-m");
		else if (strcmp ("-pf", argv[c]) == 0)
			strcpy (argv[c], "-p");
		else if (strcmp ("-gf", argv[c]) == 0)
			strcpy (argv[c], "-g");
	}

	while (1) {
		c = getopt_long (argc, argv, "hVvt:H:P:G:g:p:m:", long_opts, &opt_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?': /* args not parsable */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h': /* help */
			print_help ();
			exit (STATE_OK);
		case 'V': /* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'v': /* version */
			verbose = TRUE;
			break;
		case 't': /* timeout period */
			timeout_interval = atoi (optarg);
			break;
		case 'H': /* hostname */
			if (strlen (optarg) >= MAX_HOST_ADDRESS_LENGTH)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			server_ip = strdup (optarg);
			break;
		case 'P': /* port */
			port = atoi (optarg);
			break;
		case 'G': /* hostname */
			if (strlen (optarg) >= MAX_INPUT_BUFFER)
				die (STATE_UNKNOWN, _("Input buffer overflow\n"));
			game_type = strdup (optarg);
			break;
		case 'p': /* index of ping field */
			qstat_ping_field = atoi (optarg);
			if (qstat_ping_field < 0 || qstat_ping_field > QSTAT_MAX_RETURN_ARGS)
				return ERROR;
			break;
		case 'm': /* index on map field */
			qstat_map_field = atoi (optarg);
			if (qstat_map_field < 0 || qstat_map_field > QSTAT_MAX_RETURN_ARGS)
				return ERROR;
			break;
		case 'g': /* index of game field */
			qstat_game_field = atoi (optarg);
			if (qstat_game_field < 0 || qstat_game_field > QSTAT_MAX_RETURN_ARGS)
				return ERROR;
			break;
		case 129: /* index of player count field */
			qstat_game_players = atoi (optarg);
			if (qstat_game_players < 0 || qstat_game_players > QSTAT_MAX_RETURN_ARGS)
				return ERROR;
			break;
		case 130: /* index of max players field */
			qstat_game_players_max = atoi (optarg);
			if (qstat_game_players_max < 0 || qstat_game_players_max > QSTAT_MAX_RETURN_ARGS)
				return ERROR;
			break;
		}
	}

	c = optind;
	/* first option is the game type */
	if (!game_type && c<argc)
		game_type = strdup (argv[c++]);

	/* Second option is the server name */
	if (!server_ip && c<argc)
		server_ip = strdup (argv[c++]);

	return validate_arguments ();
}

int
validate_arguments (void)
{
		return OK;
}
