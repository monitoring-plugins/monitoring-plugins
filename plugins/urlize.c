/******************************************************************************
 *
 * urlize.c
 *
 * Program: plugin wrapper for Nagios
 * License: GPL
 * Copyright (c) 2000 Karl DeBisschop (kdebiss@alum.mit.edu)
 *
 * Last Modified: $Date$
 * 2000-06-01 Karl DeBisschop <karl@debisschop.net>
 *  Written based of concept in urlize.pl
 *
 * Usage: urlize <url> <plugin> <arg1> ... <argN>
 *
 * Description:
 *
 * This plugin wraps the text output of another command (plugin) in HTML
 * <A> tags, thus displaying the plugin output in as a clickable link in
 * the Nagios status screen.  The return status is the same as the plugin
 * invoked by urlize
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
#include "utils.h"
#include "popen.h"

void print_usage (char *);
void print_help (char *);

int
main (int argc, char **argv)
{
	int i = 0, found = 0, result = STATE_UNKNOWN;
	char command_line[MAX_INPUT_BUFFER] = "";
	char input_buffer[MAX_INPUT_BUFFER];

	if (argc < 2) {
		print_usage (my_basename (argv[0]));
		exit (STATE_UNKNOWN);
	}

	if (!strcmp (argv[1], "-h") || !strcmp (argv[1], "--help")) {
		print_help (argv[0]);
		exit (STATE_OK);
	}

	if (!strcmp (argv[1], "-V") || !strcmp (argv[1], "--version")) {
		print_revision (my_basename (argv[0]), "$Revision$");
		exit (STATE_OK);
	}

	if (argc < 2) {
		print_usage (my_basename (argv[0]));
		exit (STATE_UNKNOWN);
	}

	sprintf (command_line, "%s", argv[2]);
	for (i = 3; i < argc; i++) {
		sprintf (command_line, "%s %s", command_line, argv[i]);
	}

	child_process = spopen (command_line);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", command_line);
		exit (STATE_UNKNOWN);
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", command_line);
	}

	printf ("<A href=\"%s\">", argv[1]);
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		found++;
		if (index (input_buffer, '\n')) {
			input_buffer[strcspn (input_buffer, "\n")] = 0;
			printf ("%s", input_buffer);
		}
		else {
			printf ("%s", input_buffer);
		}
	}

	if (!found) {
		printf ("%s problem - No data recieved from host\nCMD: %s\n", argv[0],
						command_line);
		exit (STATE_UNKNOWN);
	}

	/* close the pipe */
	result = spclose (child_process);

	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	printf ("</A>\n");
	return result;
}

void
print_usage (char *cmd)
{
	printf ("Usage:\n %s <url> <plugin> <arg1> ... <argN>\n",
					my_basename (cmd));
}

void
print_help (char *cmd)
{
	print_revision ("urlize", "$Revision$");
	printf
		("Copyright (c) 2000 Karl DeBisschop (kdebiss@alum.mit.edu)\n\n"
		 "\nThis plugin wraps the text output of another command (plugin) in HTML\n"
		 "<A> tags, thus displaying the plugin output in as a clickable link in\n"
		 "the Nagios status screen.  The return status is the same as the invoked\n"
		 "plugin.\n\n");
	print_usage (cmd);
	exit (STATE_OK);
}
