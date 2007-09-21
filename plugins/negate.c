/******************************************************************************
*
* Nagios negate plugin
*
* License: GPL
* Copyright (c) 2002-2006 nagios-plugins team
*
* Last Modified: $Date$
*
* Description:
*
* This file contains the negate plugin
*
*  Negates the status of a plugin (returns OK for CRITICAL, and vice-versa)
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
* $Id$

@@-<article>

<sect1>
<title>Quick Reference</title>
<refentry>
<refmeta><manvolnum>5<manvolnum></refmeta>
<refnamdiv>
<refname>&progname;</refname>
<refpurpose>&SUMMARY;</refpurpose>
</refnamdiv>
</refentry>
</sect1>

<sect1>
<title>FAQ</title>
</sect1>

<sect1>
<title>Theory, Installation, and Operation</title>

<sect2>
<title>General Description</title>
<para>
&DESCRIPTION;
</para>
</sect2>

<sect2>
<title>Future Enhancements</title>
<para>ToDo List</para>
<itemizedlist>
<listitem>Add option to do regex substitution in output text</listitem>
</itemizedlist>
</sect2>-@@

******************************************************************************/

const char *progname = "negate";
const char *revision = "$Revision$";
const char *copyright = "2002-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#define DEFAULT_TIMEOUT 9

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"

//char *command_line;

static const char **process_arguments (int, char **);
int validate_arguments (char **);
void print_help (void);
void print_usage (void);



int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char *buf;
	char **command_line;
	output chld_out, chld_err;
	int i;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	command_line = (char **) process_arguments (argc, argv);

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR)
		die (STATE_UNKNOWN, _("Cannot catch SIGALRM"));

	(void) alarm ((unsigned) timeout_interval);

	/* catch when the command is quoted */
	if(command_line[1] == NULL) {
		result = cmd_run (command_line[0], &chld_out, &chld_err, 0);
	} else {
		result = cmd_run_array (command_line, &chld_out, &chld_err, 0);
	}
	if (chld_err.lines > 0) {
		printf ("Error output from command:\n");
		for (i = 0; i < chld_err.lines; i++) {
			printf ("%s\n", chld_err.line[i]);
		}
		exit (STATE_WARNING);
	}

	if (chld_out.lines == 0)
		die (STATE_UNKNOWN, _("No data returned from command\n"));

	for (i = 0; i < chld_out.lines; i++) {
		printf ("%s\n", chld_out.line[i]);
	}

	if (result == STATE_OK)
		exit (STATE_CRITICAL);
	else if (result == STATE_CRITICAL)
		exit (EXIT_SUCCESS);
	else
		exit (result);
}

/******************************************************************************
@@-
<sect2>
<title>Functions</title>

<sect3>
<title>process_arguments</title>

<para>This function parses the command line into the needed
variables.</para>

<para>Aside from the standard 'help' and 'version' options, there
is a only a 'timeout' option.</para>

</sect3>
-@@
******************************************************************************/



/* process command-line arguments */
static const char **
process_arguments (int argc, char **argv)
{
	int c;

	int option = 0;
	static struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{0, 0, 0, 0}
	};

	while (1) {
		c = getopt_long (argc, argv, "+hVt:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':     /* help */
			usage5 ();
			break;
		case 'h':     /* help */
			print_help ();
			exit (EXIT_SUCCESS);
			break;
		case 'V':     /* version */
			print_revision (progname, revision);
			exit (EXIT_SUCCESS);
		case 't':     /* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		}
	}

	validate_arguments (&argv[optind]);

	return (const char **) &argv[optind];
}


/******************************************************************************
@@-
<sect3>
<title>validate_arguments</title>

<para>No validation is currently done.</para>

</sect3>
-@@
******************************************************************************/



int
validate_arguments (char **command_line)
{
	if (command_line[0] == NULL)
		usage4 (_("Could not parse arguments"));

	if (strncmp(command_line[0],"/",1) != 0 && strncmp(command_line[0],"./",2) != 0)
		usage4 (_("Require path to command"));
}

/******************************************************************************
@@-
</sect2> 
</sect1>
</article>
-@@
******************************************************************************/



void
print_help (void)
{
	print_revision (progname, revision);

	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Negates the status of a plugin (returns OK for CRITICAL, and vice-versa)."));

	printf ("\n\n");

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_TIMEOUT), DEFAULT_TIMEOUT);

	printf ("    %s\n", _("[keep timeout than the plugin timeout to retain CRITICAL status]"));
	printf ("\n");
	printf ("%s\n", _("Examples:"));
	printf (" %s\n", "negate /usr/local/nagios/libexec/check_ping -H host");
	printf ("    %s\n", _("Run check_ping and invert result. Must use full path to plugin"));
	printf (" %s\n", "negate /usr/local/nagios/libexec/check_procs -a 'vi negate.c'");
	printf ("    %s\n", _("Use single quotes if you need to retain spaces"));
	printf (_(UT_VERBOSE));
	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf ("%s\n", _("This plugin is a wrapper to take the output of another plugin and invert it."));
	printf ("%s\n", _("The full path of the plugin must be provided."));
	printf ("%s\n", _("If the wrapped plugin returns STATE_OK, the wrapper will return STATE_CRITICAL."));
	printf ("%s\n", _("If the wrapped plugin returns STATE_CRITICAL, the wrapper will return STATE_OK."));
	printf ("%s\n", _("Otherwise, the output state of the wrapped plugin is unchanged."));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf (_("Usage:"));
	printf ("%s [-t timeout] <definition of wrapped plugin>\n",progname);
}
