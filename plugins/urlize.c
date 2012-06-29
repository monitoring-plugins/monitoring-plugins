/*****************************************************************************
* 
* Nagios urlize plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the urlize plugin
* 
* This plugin wraps the text output of another command (plugin) in HTML <A>
* tags, thus displaying the child plugin's output as a clickable link in the
* Nagios status screen.  This plugin returns the status of the invoked plugin.
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

const char *progname = "urlize";
const char *copyright = "2000-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "popen.h"

#define PERF_CHARACTER "|"
#define NEWLINE_CHARACTER '\n'

void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char *url = NULL;
	char *cmd;
	char *buf;
	char *nstr;
	char tstr[MAX_INPUT_BUFFER];

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

	/* Need at least 2 args */
	if (argc < 3) {
		print_help();
		exit (STATE_UNKNOWN);
	}

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
			print_revision (progname, NP_VERSION);
			exit (EXIT_SUCCESS);
			break;
		case 'u':
			url = strdup (argv[optind]);
			break;
		case '?':
		default:
			usage5 ();
		}
	}

	if (url == NULL)
		url = strdup (argv[optind++]);

	cmd = strdup (argv[optind++]);
	for (c = optind; c < argc; c++) {
		xasprintf (&cmd, "%s %s", cmd, argv[c]);
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

	bzero(tstr, sizeof(tstr));
	buf = malloc(MAX_INPUT_BUFFER);
	printf ("<A href=\"%s\">", argv[1]);
	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_process)) {
		found++;
		/* Collect the string in temp str so we can tokenize */
		strcat(tstr, buf);
	}

	if (!found)
		die (STATE_UNKNOWN,
		     _("%s UNKNOWN - No data received from host\nCMD: %s</A>\n"),
		     argv[0], cmd);


	/* chop the newline character */
	if ((nstr = strchr(tstr, NEWLINE_CHARACTER)) != NULL)
		*nstr = '\0';

	/* tokenize the string for Perfdata if there is some */
	nstr = strtok(tstr, PERF_CHARACTER);
	printf ("%s", nstr);
	printf ("</A>");
	nstr = strtok(NULL, PERF_CHARACTER);
	if (nstr != NULL)
		printf (" | %s", nstr);

	/* close the pipe */
	result = spclose (child_process);

	/* WARNING if output found on stderr */
	if (fgets (buf, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	return result;
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin wraps the text output of another command (plugin)"));
  printf ("%s\n", _("in HTML <A> tags, thus displaying the child plugin's output as a clickable link in"));
  printf ("%s\n", _("the Nagios status screen.  This plugin returns the status of the invoked plugin."));

  printf ("\n\n");

	print_usage ();

  printf (UT_HELP_VRSN);

  printf ("\n");
  printf ("%s\n", _("Examples:"));
	printf ("%s\n", _("Pay close attention to quoting to ensure that the shell passes the expected"));
  printf ("%s\n\n", _("data to the plugin. For example, in:"));
  printf (" %s\n\n", _("urlize http://example.com/ check_http -H example.com -r 'two words'"));
  printf ("    %s\n", _("the shell will remove the single quotes and urlize will see:"));
  printf (" %s\n\n", _("urlize http://example.com/ check_http -H example.com -r two words"));
  printf ("    %s\n\n", _("You probably want:"));
  printf (" %s\n", _("urlize http://example.com/ \"check_http -H example.com -r 'two words'\""));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s <url> <plugin> <arg1> ... <argN>\n", progname);
}
