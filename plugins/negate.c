/******************************************************************************
 *
 * Program: Inverting plugin wrapper for Nagios
 * License: GPL
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
 *
 *****************************************************************************/

#define PROGNAME "negate"
#define REVISION "$Revision$"
#define COPYRIGHT "2002"
#define AUTHOR "Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"
#define SUMMARY "Negates the status of a plugin (returns OK for CRITICAL, and vice-versa).\n"

#define OPTIONS "\
\[-t timeout] <definition of wrapped plugin>"

#define LONGOPTIONS "\
  -t, --timeout=INTEGER\n\
    Terminate test if timeout limit is exceeded (default: %d)\n\
     [keep this less than the plugin timeout to retain CRITICAL status]\n"

#define DESCRIPTION "\
This plugin is a wrapper to take the output of another plugin and invert it.\n\
If the wrapped plugin returns STATE_OK, the wrapper will return STATE_CRITICAL.\n\
If the wrapped plugin returns STATE_CRITICAL, the wrapper will return STATE_OK.\n\
Otherwise, the output state of the wrapped plugin is unchanged.\n"

#define DEFAULT_TIMEOUT 9

#include "common.h"
#include "utils.h"
#include "popen.h"

char *command_line;

static int process_arguments (int, char **);
static int validate_arguments (void);
static void print_usage (void);
static void print_help (void);

/******************************************************************************

The (psuedo?)literate programming XML is contained within \@\@\- <XML> \-\@\@
tags in the comments. With in the tags, the XML is assembled sequentially.
You can define entities in tags. You also have all the #defines available as
entities.

Please note that all tags must be lowercase to use the DocBook XML DTD.

@@-<article>

<sect1>
<title>Quick Reference</title>
<!-- The refentry forms a manpage -->
<refentry>
<refmeta>
<manvolnum>5<manvolnum>
</refmeta>
<refnamdiv>
<refname>&PROGNAME;</refname>
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
</sect2>


<sect2>
<title>Functions</title>
-@@
******************************************************************************/

int
main (int argc, char **argv)
{
	int found = 0, result = STATE_UNKNOWN;
	char input_buffer[MAX_INPUT_BUFFER];

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments");

	/* Set signal handling and alarm */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR)
		terminate (STATE_UNKNOWN, "Cannot catch SIGALRM");

	(void) alarm ((unsigned) timeout_interval);

	child_process = spopen (command_line);
	if (child_process == NULL)
		terminate (STATE_UNKNOWN, "Could not open pipe: %s\n", command_line);

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", command_line);
	}

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		found++;
		if (strchr (input_buffer, '\n')) {
			input_buffer[strcspn (input_buffer, "\n")] = 0;
			printf ("%s\n", input_buffer);
		}
		else {
			printf ("%s\n", input_buffer);
		}
	}

	if (!found)
		terminate (STATE_UNKNOWN,\
		           "%s problem - No data recieved from host\nCMD: %s\n",\
		           argv[0],	command_line);

	/* close the pipe */
	result = spclose (child_process);

	/* WARNING if output found on stderr */
	if (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
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




void
print_help (void)
{
	print_revision (PROGNAME, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nOptions:\n" LONGOPTIONS "\n" DESCRIPTION "\n", 
		 DEFAULT_TIMEOUT);
	support ();
}

void
print_usage (void)
{
	printf ("Usage:\n" " %s %s\n"
#ifdef HAVE_GETOPT_H
					" %s (-h | --help) for detailed help\n"
					" %s (-V | --version) for version information\n",
#else
					" %s -h for detailed help\n"
					" %s -V for version information\n",
#endif
					PROGNAME, OPTIONS, PROGNAME, PROGNAME);
}



/******************************************************************************
@@-
<sect3>
<title>process_arguments</title>

<para>This function parses the command line into the needed
variables.</para>

<para>Aside from the standard 'help' and 'version' options, there
is a only a 'timeout' option.No validation is currently done.</para>

</sect3>
-@@
******************************************************************************/

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"timeout", required_argument, 0, 't'},
		{0, 0, 0, 0}
	};
#endif

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, "+?hVt:",
		                 long_options, &option_index);
#else
		c = getopt (argc, argv, "+?hVt:");
#endif
		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':     /* help */
			usage2 ("Unknown argument", optarg);
		case 'h':     /* help */
			print_help ();
			exit (EXIT_SUCCESS);
		case 'V':     /* version */
			print_revision (PROGNAME, REVISION);
			exit (EXIT_SUCCESS);
		case 't':     /* timeout period */
			if (!is_integer (optarg))
				usage2 ("Timeout Interval must be an integer", optarg);
			timeout_interval = atoi (optarg);
			break;
		}
	}

	command_line = strscpy (command_line, argv[optind]);
	for (c = optind+1; c <= argc; c++) {
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
	return STATE_OK;
}


/******************************************************************************
@@-
</sect2> 
</sect1>
</article>
-@@
******************************************************************************/
