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
const char *copyright = "2002-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#define DEFAULT_TIMEOUT 9

#include "common.h"
#include "utils.h"
#include "popen.h"

char *command_line;

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char *buf;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) == ERROR)
		usage (_("Could not parse arguments\n"));

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR)
		die (STATE_UNKNOWN, _("Cannot catch SIGALRM"));

	(void) alarm ((unsigned) timeout_interval);

	child_process = spopen (command_line);
	if (child_process == NULL)
		die (STATE_UNKNOWN, _("Could not open pipe: %s\n"), command_line);

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), command_line);
	}

	buf = malloc(MAX_INPUT_BUFFER);
	while (fgets (buf, MAX_INPUT_BUFFER - 1, child_process)) {
		found++;
		printf ("%s", buf);
	}

	if (!found)
		die (STATE_UNKNOWN,
		     _("%s problem - No data received from host\nCMD: %s\n"),\
		     argv[0], command_line);

	/* close the pipe */
	result = spclose (child_process);

	/* WARNING if output found on stderr */
	if (fgets (buf, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

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
int
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
		c = getopt_long (argc, argv, "+hVt:",
		                 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':     /* help */
			usage3 (_("Unknown argument"), optopt);
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
				usage2 (_("Timeout Interval must be an integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		}
	}

	asprintf (&command_line, "%s", argv[optind]);
	for (c = optind+1; c < argc; c++) {
		asprintf (&command_line, "%s %s", command_line, argv[c]);
	}

	return validate_arguments ();
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
validate_arguments ()
{
	if (command_line == NULL)
		return ERROR;
	return STATE_OK;
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

	printf (_(COPYRIGHT), copyright, email);

	printf (_("\
Negates the status of a plugin (returns OK for CRITICAL, and vice-versa).\n\
\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_TIMEOUT), DEFAULT_TIMEOUT);

	printf (_("\
     [keep timeout than the plugin timeout to retain CRITICAL status]\n"));

	printf (_("\
  negate \"/usr/local/nagios/libexec/check_ping -H host\"\n\
    Run check_ping and invert result. Must use full path to plugin\n\
  negate \"/usr/local/nagios/libexec/check_procs -a 'vi negate.c'\"\n\
    Use single quotes if you need to retain spaces\n"));

	printf (_("\
This plugin is a wrapper to take the output of another plugin and invert it.\n\
If the wrapped plugin returns STATE_OK, the wrapper will return STATE_CRITICAL.\n\
If the wrapped plugin returns STATE_CRITICAL, the wrapper will return STATE_OK.\n\
Otherwise, the output state of the wrapped plugin is unchanged.\n"));

	printf (_(UT_SUPPORT));
}





void
print_usage (void)
{
	printf (_("Usage: %s [-t timeout] <definition of wrapped plugin>\n"),
	        progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
