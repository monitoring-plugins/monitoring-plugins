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

 $Id$
 
******************************************************************************/

const char *progname = "urlize";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "popen.h"

void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char *url = NULL;
	char *cmd;
	char *buf;

	int c;
	int option = 0;
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"url", required_argument, 0, 'u'},
		{0, 0, 0, 0}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	while (1) {
		c = getopt_long (argc, argv, "+hVu:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'h':     /* help */
			print_help ();
			exit (EXIT_SUCCESS);
			break;
		case 'V':     /* version */
			print_revision (progname, revision);
			exit (EXIT_SUCCESS);
			break;
		case 'u':
			url = strdup (argv[optind]);
			break;
		case '?':
		default:
			usage2 (_("Unknown argument"), optarg);
		}
	}

	if (url == NULL)
		url = strdup (argv[optind++]);

	cmd = strdup (argv[optind++]);
	for (c = optind; c < argc; c++) {
		asprintf (&cmd, "%s %s", cmd, argv[c]);
	}

	child_process = spopen (cmd);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), cmd);
		exit (STATE_UNKNOWN);
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), cmd);
	}

	buf = malloc(MAX_INPUT_BUFFER);
	printf ("<A href=\"%s\">", argv[1]);
	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_process)) {
		found++;
		printf ("%s", buf);
	}

	if (!found)
		die (STATE_UNKNOWN,
		     _("%s UNKNOWN - No data received from host\nCMD: %s</A>\n"),
		     argv[0], cmd);

	/* close the pipe */
	result = spclose (child_process);

	/* WARNING if output found on stderr */
	if (fgets (buf, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	printf ("</A>\n");
	return result;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("\n\
This plugin wraps the text output of another command (plugin) in HTML\n\
<A> tags, thus displaying the plugin output in as a clickable link in\n\
the Nagios status screen.  The return status is the same as the invoked\n\
plugin.\n\n"));

	print_usage ();

	printf (_("\n\
Pay close attention to quoting to ensure that the shell passes the expected\n\
data to the plugin. For example, in:\n\
\n\
    urlize http://example.com/ check_http -H example.com -r 'two words'\n\
\n\
the shell will remove the single quotes and urlize will see:\n\
\n\
    urlize http://example.com/ check_http -H example.com -r two words\n\
\n\
You probably want:\n\
\n\
    urlize http://example.com/ \"check_http -H example.com -r 'two words'\"\n"));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("Usage:\n %s <url> <plugin> <arg1> ... <argN>\n", progname);
}
