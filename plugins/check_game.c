/******************************************************************************
 *
 * CHECK_GAME.C
 *
 * Program: GAME plugin for Nagios
 * License: GPL
 * Copyright (c) 1999 Ian Cass (ian@knowledge.com)
 *
 * Last Modified: $Date$
 *
 * Mod History
 *
 * 25-8-99 Ethan Galstad <nagios@nagios.org>
 *	   Integrated with common plugin code, minor cleanup stuff
 *
 * 17-8-99 version 1.1b
 *
 * 17-8-99 make port a separate argument so we can use something like
 *         check_game q2s!27910 with the probe set up as
 *         check_game $ARG1$ $HOSTADDRESS$ $ARG2$
 *
 * 17-8-99 Put in sanity check for ppl who enter the wrong server type
 *
 * 17-8-99 Release version 1.0b
 *
 * Command line: CHECK_GAME <server type> <ip_address> [port]
 *
 * server type = a server type that qstat understands (type qstat & look at the -default line)
 * ip_address  = either a dotted address or a FQD name
 * port        = defaults game default port
 *                        
 *
 * Description:
 * 
 * Needed to explore writing my own probes for nagios. It looked
 * pretty simple so I thought I'd write one for monitoring the status
 * of game servers. It uses qstat to do the actual monitoring and
 * analyses the result. Doing it this way means I can support all the
 * servers that qstat does and will do in the future.
 *
 *
 * Dependencies:
 *
 * This plugin uses the 'qstat' command If you don't
 * have the package installed you will need to download it from 
 * http://www.activesw.com/people/steve/qstat.html or any popular files archive
 * before you can use this plugin.
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
#include "utils.h"

int process_arguments (int, char **);
const char *progname = "check_game";

#define QSTAT_DATA_DELIMITER 	","

#define QSTAT_HOST_ERROR	"ERROR"
#define QSTAT_HOST_DOWN		"DOWN"
#define QSTAT_HOST_TIMEOUT	"TIMEOUT"
#define QSTAT_MAX_RETURN_ARGS	12

char server_ip[MAX_HOST_ADDRESS_LENGTH];
char game_type[MAX_INPUT_BUFFER];
char port[MAX_INPUT_BUFFER];

int qstat_game_players_max = 4;
int qstat_game_players = 5;
int qstat_game_field = 2;
int qstat_map_field = 3;
int qstat_ping_field = 5;


int
main (int argc, char **argv)
{
	char command_line[MAX_INPUT_BUFFER];
	int result;
	FILE *fp;
	char input_buffer[MAX_INPUT_BUFFER];
	char response[MAX_INPUT_BUFFER];
	char *temp_ptr;
	int found;
	char *p, *ret[QSTAT_MAX_RETURN_ARGS];
	int i;

	result = process_arguments (argc, argv);

	if (result != OK) {
		printf ("Incorrect arguments supplied\n");
		printf ("\n");
		print_revision (argv[0], "$Revision$");
		printf ("Copyright (c) 1999 Ian Cass, Knowledge Matters Limited\n");
		printf ("License: GPL\n");
		printf ("\n");
		printf
			("Usage: %s <game> <ip_address> [-p port] [-gf game_field] [-mf map_field] [-pf ping_field]\n",
			 argv[0]);
		printf ("\n");
		printf ("Options:\n");
		printf
			(" <game>        = Game type that is recognised by qstat (without the leading dash)\n");
		printf
			(" <ip_address>  = The IP address of the device you wish to query\n");
		printf (" [port]        = Optional port of which to connect\n");
		printf
			(" [game_field]  = Field number in raw qstat output that contains game name\n");
		printf
			(" [map_field]   = Field number in raw qstat output that contains map name\n");
		printf
			(" [ping_field]  = Field number in raw qstat output that contains ping time\n");
		printf ("\n");
		printf ("Notes:\n");
		printf
			("- This plugin uses the 'qstat' command, the popular game server status query tool .\n");
		printf
			("  If you don't have the package installed, you will need to download it from\n");
		printf
			("  http://www.activesw.com/people/steve/qstat.html before you can use this plugin.\n");
		printf ("\n");
		return STATE_UNKNOWN;
	}

	result = STATE_OK;

	/* create the command line to execute */
	snprintf (command_line, sizeof (command_line) - 1, "%s -raw %s -%s %s%s",
						PATH_TO_QSTAT, QSTAT_DATA_DELIMITER, game_type, server_ip, port);
	command_line[sizeof (command_line) - 1] = 0;

	/* run the command */
	fp = popen (command_line, "r");
	if (fp == NULL) {
		printf ("Error - Could not open pipe: %s\n", command_line);
		return STATE_UNKNOWN;
	}

	found = 0;
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
		printf ("ERROR: Host type parameter incorrect!\n");
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
		ret[qstat_game_players_max],
		ret[qstat_game_players],
                ret[qstat_game_field], 
		ret[qstat_map_field],
		ret[qstat_ping_field]);
	}

	/* close the pipe */
	pclose (fp);

	return result;
}



int
process_arguments (int argc, char **argv)
{
	int x;

	/* not enough options were supplied */
	if (argc < 3)
		return ERROR;

	/* first option is always the game type */
	strncpy (game_type, argv[1], sizeof (game_type) - 1);
	game_type[sizeof (game_type) - 1] = 0;

	/* Second option is always the server name */
	strncpy (server_ip, argv[2], sizeof (server_ip) - 1);
	server_ip[sizeof (server_ip) - 1] = 0;

	/* process all remaining arguments */
	for (x = 4; x <= argc; x++) {

		/* we got the port number to connect to */
		if (!strcmp (argv[x - 1], "-p")) {
			if (x < argc) {
				snprintf (port, sizeof (port) - 2, ":%s", argv[x]);
				port[sizeof (port) - 1] = 0;
				x++;
			}
			else
				return ERROR;
		}

		/* we got the game field */
		else if (!strcmp (argv[x - 1], "-gf")) {
			if (x < argc) {
				qstat_game_field = atoi (argv[x]);
				if (qstat_game_field < 0 || qstat_game_field > QSTAT_MAX_RETURN_ARGS)
					return ERROR;
				x++;
			}
			else
				return ERROR;
		}

		/* we got the map field */
		else if (!strcmp (argv[x - 1], "-mf")) {
			if (x < argc) {
				qstat_map_field = atoi (argv[x]);
				if (qstat_map_field < 0 || qstat_map_field > QSTAT_MAX_RETURN_ARGS)
					return ERROR;
				x++;
			}
			else
				return ERROR;
		}

		/* we got the ping field */
		else if (!strcmp (argv[x - 1], "-pf")) {
			if (x < argc) {
				qstat_ping_field = atoi (argv[x]);
				if (qstat_ping_field < 0 || qstat_ping_field > QSTAT_MAX_RETURN_ARGS)
					return ERROR;
				x++;
			}
			else
				return ERROR;
		}

		/* else we got something else... */
		else
			return ERROR;
	}

	return OK;
}

void print_usage (void)
{
	return STATE_OK;
}
