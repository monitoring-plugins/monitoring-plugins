/******************************************************************************
 *
 * CHECK_VSZ.C
 *
 * Program: Process plugin for Nagios
 * License: GPL
 * Copyright (c) 1999,2000 Karl DeBisschop <kdebiss@alum.mit.edu>
 *
 * Last Modified: $Date$
 *
 * Description:
 *
 * This plugin will check for processes whose total image size exceeds
 * the warning or critical thresholds given on the command line.   With
 * no command_name, everything that shows up on ps is evaluated.
 * Otherwise, only jobs with the command_name given are examined.
 * This program is particularly useful if you have to run a piece of
 * commercial software that has a memory leak.  With it you can shut
 * down and restart the processes whenever the program threatens to
 * take over your system.
 *
 * Modifications:
 *
 * 11-18-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
 *            change to getopt, use print_help
 * 08-18-1999 Ethan Galstad (nagios@nagios.org)
 *            Changed code to use common include file
 *            Changed fclose() to pclose()
 * 09-09-1999 Ethan Galstad (nagios@nagios.org)
 *            Changed popen()/pclose() to spopen()/spclose()
 * 11-18-1999 Karl DeBisschop (kdebiss@alum.mit.edu)
 *            set STATE_WARNING of stderr written or nonzero status returned
 *
 *****************************************************************************/

const char *progname = "check_vsz";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2002"
#define AUTHOR "Karl DeBisschop"
#define EMAIL "karl@debisschop.net"
#define SUMMARY "Check the image size of a running program.\n"

#include "common.h"
#include "popen.h"
#include "utils.h"

int process_arguments (int argc, char **argv);
void print_help (const char *cmd);
void print_usage (const char *cmd);

int warn = -1;
int crit = -1;
char *proc = NULL;

int
main (int argc, char **argv)
{
	int len;
	int result = STATE_OK;
	int line = 0;
	int proc_size = -1;
	char input_buffer[MAX_INPUT_BUFFER];
	char proc_name[MAX_INPUT_BUFFER];
	char *message = "";

	if (process_arguments (argc, argv) == ERROR) {
		printf ("%s: failure parsing arguments\n", progname);
		print_help (progname);
		return STATE_UNKNOWN;
	}

	/* run the command */
	child_process = spopen (VSZ_COMMAND);
	if (child_process == NULL) {
		printf ("Unable to open pipe: %s\n", VSZ_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", VSZ_COMMAND);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {

		line++;

		/* skip the first line */
		if (line == 1)
			continue;

		if (sscanf (input_buffer, VSZ_FORMAT, &proc_size, proc_name) == 2) {
			if (proc == NULL) {
				if (proc_size > warn) {
					asprintf (&message, "%s %s(%d)", message, proc_name, proc_size);
					result = max_state (result, STATE_WARNING);
				}
				if (proc_size > crit) {
					result = STATE_CRITICAL;
				}
			}
			else if (strstr (proc_name, proc)) {
				asprintf (&message, "%s %d", message, proc_size);
				if (proc_size > warn) {
					result = max_state (result, STATE_WARNING);
				}
				if (proc_size > crit) {
					result = STATE_CRITICAL;
				}
			}
		}
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);

	if (result == STATE_OK)
		printf ("ok (all VSZ<%d): %s\n", warn, message);
	else if (result == STATE_UNKNOWN)
		printf ("Unable to read output\n");
	else if (result == STATE_WARNING)
		printf ("WARNING (VSZ>%d):%s\n", warn, message);
	else
		printf ("CRITICAL (VSZ>%d):%s\n", crit, message);

	return result;
}




int
process_arguments (int argc, char **argv)
{
	int c;

	int option_index = 0;
	static struct option long_options[] = {
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"command", required_argument, 0, 'C'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "+hVc:w:C:", long_options, &option_index);

		if (c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			print_usage (progname);
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help (progname);
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'c':									/* critical threshold */
			if (!is_intnonneg (optarg)) {
				printf ("%s: critical threshold must be an integer: %s\n",
								progname, optarg);
				print_usage (progname);
				exit (STATE_UNKNOWN);
			}
			crit = atoi (optarg);
			break;
		case 'w':									/* warning threshold */
			if (!is_intnonneg (optarg)) {
				printf ("%s: warning threshold must be an integer: %s\n",
								progname, optarg);
				print_usage (progname);
				exit (STATE_UNKNOWN);
			}
			warn = atoi (optarg);
			break;
		case 'C':									/* command name */
			proc = optarg;
			break;
		}
	}

	c = optind;
	if (warn == -1) {
		if (!is_intnonneg (argv[c])) {
			printf ("%s: critical threshold must be an integer: %s\n",
							progname, argv[c]);
			print_usage (progname);
			exit (STATE_UNKNOWN);
		}
		warn = atoi (argv[c++]);
	}

	if (crit == -1) {
		if (!is_intnonneg (argv[c])) {
			printf ("%s: critical threshold must be an integer: %s\n",
							progname, argv[c]);
			print_usage (progname);
			exit (STATE_UNKNOWN);
		}
		crit = atoi (argv[c++]);
	}

	if (proc == NULL)
		proc = argv[c];

	return c;
}

void
print_usage (const char *cmd)
{
	printf ("Usage: %s -w <wsize> -c <csize> [-C command]\n"
					"       %s --help\n" "       %s --version\n", cmd, cmd, cmd);
}

void
print_help (const char *cmd)
{
	print_revision ("check_vsz", "$Revision$");
	printf
		("Copyright (c) 2000 Karl DeBisschop <kdebiss@alum.mit.edu>\n\n"
		 "This plugin checks the image size of a running program and returns an\n"
		 "error if the number is above either of the thresholds given.\n\n");
	print_usage (cmd);
	printf
		("\nOptions:\n"
		 " -h, --help\n"
		 "    Print detailed help\n"
		 " -V, --version\n"
		 "    Print version numbers and license information\n"
		 " -w, --warning=INTEGER\n"
		 "    Program image size necessary to cause a WARNING state\n"
		 " -c, --critical=INTEGER\n"
		 "    Program image size necessary to cause a CRITICAL state\n"
		 " -C, --command=STRING\n" "    Program to search for [optional]\n");
}
