/******************************************************************************
*
* CHECK_PROCS.C
*
* Program: Process plugin for Nagios
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* $Id$
*
* Description:
*
* This plugin checks the number of currently running processes and
* generates WARNING or CRITICAL states if the process count is outside
* the specified threshold ranges. The process count can be filtered by
* process owner, parent process PID, current state (e.g., 'Z'), or may
* be the total number of running processes
*
* License Information:
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
******************************************************************************/

const char *progname = "check_procs";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2002"
#define AUTHOR "Ethan Galstad"
#define EMAIL "nagios@nagios.org"
#define SUMMARY "Check the number of currently running processes and generates WARNING or\n\
CRITICAL states if the process count is outside the specified threshold\n\
ranges. The process count can be filtered by process owner, parent process\n\
PID, current state (e.g., 'Z'), or may be the total number of running\n\
processes\n"

#include "config.h"
#include <pwd.h>
#include "common.h"
#include "popen.h"
#include "utils.h"

int process_arguments (int, char **);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

int wmax = -1;
int cmax = -1;
int wmin = -1;
int cmin = -1;

int options = 0; /* bitmask of filter criteria to test against */
#define ALL 1
#define STAT 2
#define PPID 4
#define USER 8
#define PROG 16
#define ARGS 32

int verbose = FALSE;
int uid;
int ppid;
char *statopts = "";
char *prog = "";
char *args = "";
char *fmt = "";
char tmp[MAX_INPUT_BUFFER];
const char *zombie = "Z";

int
main (int argc, char **argv)
{
	char input_buffer[MAX_INPUT_BUFFER];

	int procuid = 0;
	int procppid = 0;
	char procstat[8];
	char procprog[MAX_INPUT_BUFFER];
	char *procargs;

	int resultsum = 0; /* bitmask of the filter criteria met by a process */
	int found = 0; /* counter for number of lines returned in `ps` output */
	int procs = 0; /* counter for number of processes meeting filter criteria */
	int pos; /* number of spaces before 'args' in `ps` output */
	int cols; /* number of columns in ps output */

	int result = STATE_UNKNOWN;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Unable to parse command line\n");

	/* run the command */
	if (verbose)
		printf ("%s\n", PS_COMMAND);
	child_process = spopen (PS_COMMAND);
	if (child_process == NULL) {
		printf ("Could not open pipe: %s\n", PS_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf ("Could not open stderr for %s\n", PS_COMMAND);

	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		cols = sscanf (input_buffer, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		if ( cols == 3 && strstr(procstat, zombie) ) {
			strcpy(procprog, "");
			cols = 4;
		}
		if ( cols >= 4 ) {
			found++;
			resultsum = 0;
			asprintf (&procargs, "%s", input_buffer + pos);
 			strip (procargs);
			if ((options & STAT) && (strstr (statopts, procstat)))
				resultsum |= STAT;
			if ((options & ARGS) && procargs && (strstr (procargs, args) == procargs))
				resultsum |= ARGS;
			if ((options & PROG) && procprog && (strcmp (prog, procprog) == 0))
				resultsum |= PROG;
			if ((options & PPID) && (procppid == ppid))
				resultsum |= PPID;
			if ((options & USER) && (procuid == uid))
				resultsum |= USER;
#ifdef DEBUG1
			if (procargs == NULL)
				printf ("%d %d %d %s %s\n", procs, procuid, procppid, procstat,
								procprog);
			else
				printf ("%d %d %d %s %s %s\n", procs, procuid, procppid, procstat,
								procprog, procargs);
#endif
			if (options == resultsum)
				procs++;
		} 
		/* This should not happen */
		else if (verbose) {
			printf("Not parseable: %s", input_buffer);
		}
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (verbose)
			printf ("STDERR: %s", input_buffer);
		result = max_state (result, STATE_WARNING);
		printf ("System call sent warnings to stderr\n");
	}
	
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		printf ("System call returned nonzero status\n");
		result = max_state (result, STATE_WARNING);
	}

	if (options == ALL)
		procs = found;

	if (found == 0) {							/* no process lines parsed so return STATE_UNKNOWN */
		printf ("Unable to read output\n");

		return result;
	}

	if (verbose && (options & STAT))
		printf ("%s ", statopts);
	if (verbose && (options & PROG))
		printf ("%s ", prog);
	if (verbose && (options & PPID))
		printf ("%d ", ppid);
	if (verbose && (options & USER))
		printf ("%d ", uid);

 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) {
		if (result == STATE_UNKNOWN)
			result = STATE_OK;
		printf (fmt, "OK", procs);
		return result;
 	}
	else if (cmax >= 0 && cmin >= 0 && cmax < cmin) {
		if (procs > cmax && procs < cmin) {
			printf (fmt, "CRITICAL", procs);
			return STATE_CRITICAL;
		}
	}
	else if (cmax >= 0 && procs > cmax) {
		printf (fmt, "CRITICAL", procs);
		return STATE_CRITICAL;
	}
	else if (cmin >= 0 && procs < cmin) {
		printf (fmt, "CRITICAL", procs);
		return STATE_CRITICAL;
	}

	if (wmax >= 0 && wmin >= 0 && wmax < wmin) {
		if (procs > wmax && procs < wmin) {
			printf (fmt, "CRITICAL", procs);
			return STATE_CRITICAL;
		}
	}
	else if (wmax >= 0 && procs > wmax) {
		printf (fmt, "WARNING", procs);
		return max_state (result, STATE_WARNING);
	}
	else if (wmin >= 0 && procs < wmin) {
		printf (fmt, "WARNING", procs);
		return max_state (result, STATE_WARNING);
	}

	printf (fmt, "OK", procs);
	if ( result == STATE_UNKNOWN ) {
		result = STATE_OK;
	}
	return result;
}

/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 1;
	char *user;
	struct passwd *pw;
	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"timeout", required_argument, 0, 't'},
		{"status", required_argument, 0, 's'},
		{"ppid", required_argument, 0, 'p'},
		{"command", required_argument, 0, 'C'},
		{"argument-array", required_argument, 0, 'a'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvht:c:w:p:s:u:C:a:", long_options, &option_index);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, REVISION);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg)) {
				printf ("%s: Timeout Interval must be an integer!\n\n",
				        progname);
				print_usage ();
				exit (STATE_UNKNOWN);
			}
			timeout_interval = atoi (optarg);
			break;
		case 'c':									/* critical threshold */
			if (is_integer (optarg)) {
				cmax = atoi (optarg);
				break;
			}
			else if (sscanf (optarg, ":%d", &cmax) == 1) {
				break;
			}
			else if (sscanf (optarg, "%d:%d", &cmin, &cmax) == 2) {
				break;
			}
			else if (sscanf (optarg, "%d:", &cmin) == 1) {
				break;
			}
			else {
				printf ("%s: Critical Process Count must be an integer!\n\n",
				        progname);
				print_usage ();
				exit (STATE_UNKNOWN);
			}
		case 'w':									/* warning time threshold */
			if (is_integer (optarg)) {
				wmax = atoi (optarg);
				break;
			}
			else if (sscanf (optarg, ":%d", &wmax) == 1) {
				break;
			}
			else if (sscanf (optarg, "%d:%d", &wmin, &wmax) == 2) {
				break;
			}
			else if (sscanf (optarg, "%d:", &wmin) == 1) {
				break;
			}
			else {
				printf ("%s: Warning Process Count must be an integer!\n\n",
				        progname);
				print_usage ();
				exit (STATE_UNKNOWN);
			}
		case 'p':									/* process id */
			if (sscanf (optarg, "%d%[^0-9]", &ppid, tmp) == 1) {
				asprintf (&fmt, "%s%sPPID = %d", fmt, (options ? ", " : ""), ppid);
				options |= PPID;
				break;
			}
			printf ("%s: Parent Process ID must be an integer!\n\n",
			        progname);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 's':									/* status */
			asprintf (&statopts, "%s", optarg);
			asprintf (&fmt, "%s%sSTATE = %s", fmt, (options ? ", " : ""), statopts);
			options |= STAT;
			break;
		case 'u':									/* user or user id */
			if (is_integer (optarg)) {
				uid = atoi (optarg);
				pw = getpwuid ((uid_t) uid);
				/*  check to be sure user exists */
				if (pw == NULL) {
					printf ("UID %d was not found\n", uid);
					print_usage ();
					exit (STATE_UNKNOWN);
				}
			}
			else {
				pw = getpwnam (optarg);
				/*  check to be sure user exists */
				if (pw == NULL) {
					printf ("User name %s was not found\n", optarg);
					print_usage ();
					exit (STATE_UNKNOWN);
				}
				/*  then get uid */
				uid = pw->pw_uid;
			}
			user = pw->pw_name;
			asprintf (&fmt, "%s%sUID = %d (%s)", fmt, (options ? ", " : ""),
			          uid, user);
			options |= USER;
			break;
		case 'C':									/* command */
			asprintf (&prog, "%s", optarg);
			asprintf (&fmt, "%s%scommand name %s", fmt, (options ? ", " : ""),
			          prog);
			options |= PROG;
			break;
		case 'a':									/* args (full path name with args) */
			asprintf (&args, "%s", optarg);
			asprintf (&fmt, "%s%sargs %s", fmt, (options ? ", " : ""), args);
			options |= ARGS;
			break;
		case 'v':									/* command */
			verbose = TRUE;
			break;
		}
	}

	c = optind;
	if (wmax == -1 && argv[c])
		wmax = atoi (argv[c++]);
	if (cmax == -1 && argv[c])
		cmax = atoi (argv[c++]);
	if (statopts == NULL && argv[c]) {
		asprintf (&statopts, "%s", argv[c++]);
		asprintf (&fmt, "%s%sSTATE = %s", fmt, (options ? ", " : ""), statopts);
		options |= STAT;
	}

	return validate_arguments ();
}


int
validate_arguments ()
{

if (wmax >= 0 && wmin == -1)
		wmin = 0;
	if (cmax >= 0 && cmin == -1)
		cmin = 0;
	if (wmax >= wmin && cmax >= cmin) {	/* standard ranges */
		if (wmax > cmax && cmax != -1) {
			printf ("wmax (%d) cannot be greater than cmax (%d)\n", wmax, cmax);
			return ERROR;
		}
		if (cmin > wmin && wmin != -1) {
			printf ("wmin (%d) cannot be less than cmin (%d)\n", wmin, cmin);
			return ERROR;
		}
	}

/* 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) { */
/* 		printf ("At least one threshold must be set\n"); */
/* 		return ERROR; */
/* 	} */

	if (options == 0) {
		options = 1;
		asprintf (&fmt, "%%s - %%d processes running\n");
	}
	else {
		asprintf (&fmt, "%%s - %%d processes running with %s\n", fmt);
	}

	return options;
}


void
print_help (void)
{
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHOR, EMAIL, SUMMARY);
	print_usage ();
	printf
		("\nRequired Arguments:\n"
		 " -w, --warning=RANGE\n"
		 "    generate warning state if process count is outside this range\n"
		 " -c, --critical=RANGE\n"
		 "    generate critical state if process count is outside this range\n\n"
		 "Optional Filters:\n"
		 " -s, --state=STATUSFLAGS\n"
		 "    Only scan for processes that have, in the output of `ps`, one or\n"
		 "    more of the status flags you specify (for example R, Z, S, RS,\n"
		 "    RSZDT, plus others based on the output of your 'ps' command).\n"
		 " -p, --ppid=PPID\n"
		 "    Only scan for children of the parent process ID indicated.\n"
		 " -u, --user=USER\n"
		 "    Only scan for proceses with user name or ID indicated.\n"
		 " -a, --argument-array=STRING\n"
		 "    Only scan for ARGS that match up to the length of the given STRING\n"
		 " -C, --command=COMMAND\n"
		 "    Only scan for exact matches to the named COMMAND.\n\n"
		 "RANGEs are specified 'min:max' or 'min:' or ':max' (or 'max'). If\n"
		 "specified 'max:min', a warning status will be generated if the\n"

		 "count is inside the specified range\n");}


void
print_usage (void)
{
	printf
		("Usage:\n"
		 " check_procs -w <range> -c <range> [-s state] [-p ppid] [-u user]\n"
		 "             [-a argument-array] [-C command]\n"
		 " check_procs --version\n" " check_procs --help\n");
}
