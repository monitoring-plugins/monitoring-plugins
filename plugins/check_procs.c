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

******************************************************************************/

const char *progname = "check_procs";
const char *revision = "$Revision$";
const char *copyright = "2000-2003";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"
#include <pwd.h>

int process_arguments (int, char **);
int validate_arguments (void);
int check_thresholds (int);
void print_help (void);
void print_usage (void);

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
#define VSZ  64
#define RSS  128
#define PCPU 256

/* Different metrics */
char *metric_name;
enum metric {
	METRIC_PROCS,
	METRIC_VSZ,
	METRIC_RSS,
	METRIC_CPU
};
enum metric metric = METRIC_PROCS;

int verbose = 0;
int uid;
int ppid;
int vsz;
int rss;
float pcpu;
char *statopts;
char *prog;
char *args;
char *fmt;
char *fails;
char tmp[MAX_INPUT_BUFFER];






int
main (int argc, char **argv)
{
	char *input_buffer;
	char *input_line;
	char *procprog;

	int procuid = 0;
	int procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	float procpcpu = 0;
	char procstat[8];
	char *procargs;
	char *temp_string;

	const char *zombie = "Z";

	int resultsum = 0; /* bitmask of the filter criteria met by a process */
	int found = 0; /* counter for number of lines returned in `ps` output */
	int procs = 0; /* counter for number of processes meeting filter criteria */
	int pos; /* number of spaces before 'args' in `ps` output */
	int cols; /* number of columns in ps output */
	int expected_cols = PS_COLS - 1;
	int warn = 0; /* number of processes in warn state */
	int crit = 0; /* number of processes in crit state */
	int i = 0;

	int result = STATE_UNKNOWN;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	input_buffer = malloc (MAX_INPUT_BUFFER);
	procprog = malloc (MAX_INPUT_BUFFER);

	asprintf (&metric_name, "PROCS");
	metric = METRIC_PROCS;

	if (process_arguments (argc, argv) == ERROR)
		usage (_("Unable to parse command line\n"));

	if (verbose >= 2)
		printf (_("CMD: %s\n"), PS_COMMAND);

	child_process = spopen (PS_COMMAND);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), PS_COMMAND);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), PS_COMMAND);

	/* flush first line */
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	while ( input_buffer[strlen(input_buffer)-1] != '\n' )
		fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		asprintf (&input_line, "%s", input_buffer);
		while ( input_buffer[strlen(input_buffer)-1] != '\n' ) {
			fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
			asprintf (&input_line, "%s%s", input_line, input_buffer);
		}

		if (verbose >= 3)
			printf ("%s", input_line);

		strcpy (procprog, "");
		asprintf (&procargs, "%s", "");

		cols = sscanf (input_line, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		if ( cols == (expected_cols - 1) && strstr(procstat, zombie) ) {
			cols = expected_cols;
			/* Set some value for procargs for the strip command further below 
			Seen to be a problem on some Solaris 7 and 8 systems */
			input_buffer[pos] = '\n';
			input_buffer[pos+1] = 0x0;
		}
		if ( cols >= expected_cols ) {
			resultsum = 0;
			asprintf (&procargs, "%s", input_line + pos);
			strip (procargs);

			/* Some ps return full pathname for command. This removes path */
			temp_string = strtok ((char *)procprog, "/");
			while (temp_string) {
				strcpy(procprog, temp_string);
				temp_string = strtok (NULL, "/");
			}

			if (verbose >= 3)
				printf ("%d %d %d %d %d %.2f %s %s %s\n", 
					procs, procuid, procvsz, procrss,
					procppid, procpcpu, procstat, procprog, procargs);

			/* Ignore self */
			if (strcmp (procprog, progname) == 0) {
				continue;
			}

			if ((options & STAT) && (strstr (statopts, procstat)))
				resultsum |= STAT;
			if ((options & ARGS) && procargs && (strstr (procargs, args) != NULL))
				resultsum |= ARGS;
			if ((options & PROG) && procprog && (strcmp (prog, procprog) == 0))
				resultsum |= PROG;
			if ((options & PPID) && (procppid == ppid))
				resultsum |= PPID;
			if ((options & USER) && (procuid == uid))
				resultsum |= USER;
			if ((options & VSZ)  && (procvsz >= vsz))
				resultsum |= VSZ;
			if ((options & RSS)  && (procrss >= rss))
				resultsum |= RSS;
			if ((options & PCPU)  && (procpcpu >= pcpu))
				resultsum |= PCPU;

			found++;

			/* Next line if filters not matched */
			if (!(options == resultsum || options == ALL))
				continue;

			procs++;

			if (metric == METRIC_VSZ)
				i = check_thresholds (procvsz);
			else if (metric == METRIC_RSS)
				i = check_thresholds (procrss);
			/* TODO? float thresholds for --metric=CPU */
			else if (metric == METRIC_CPU)
				i = check_thresholds ((int)procpcpu); 

			if (metric != METRIC_PROCS) {
				if (i == STATE_WARNING) {
					warn++;
					asprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
				if (i == STATE_CRITICAL) {
					crit++;
					asprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
			}
		} 
		/* This should not happen */
		else if (verbose) {
			printf(_("Not parseable: %s"), input_buffer);
		}
	}

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		if (verbose)
			printf (_("STDERR: %s"), input_buffer);
		result = max_state (result, STATE_WARNING);
		printf (_("System call sent warnings to stderr\n"));
	}
	
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process)) {
		printf (_("System call returned nonzero status\n"));
		result = max_state (result, STATE_WARNING);
	}

	if (found == 0) {							/* no process lines parsed so return STATE_UNKNOWN */
		printf (_("Unable to read output\n"));
		return result;
	}

	if ( result == STATE_UNKNOWN ) 
		result = STATE_OK;

	/* Needed if procs found, but none match filter */
	if ( metric == METRIC_PROCS ) {
		result = max_state (result, check_thresholds (procs) );
	}

	if ( result == STATE_OK ) {
		printf ("%s %s: ", metric_name, _("OK"));
	} else if (result == STATE_WARNING) {
		printf ("%s %s: ", metric_name, _("WARNING"));
		if ( metric != METRIC_PROCS ) {
			printf (_("%d warn out of "), warn);
		}
	} else if (result == STATE_CRITICAL) {
		printf ("%s %s: ", metric_name, _("CRITICAL"));
		if (metric != METRIC_PROCS) {
			printf (_("%d crit, %d warn out of "), crit, warn);
		}
	} 
	printf (ngettext ("%d process", "%d processes", (unsigned long) procs), procs);
	
	if (strcmp(fmt,"") != 0) {
		printf (_(" with %s"), fmt);
	}

	if ( verbose >= 1 && strcmp(fails,"") )
		printf (" [%s]", fails);

	printf ("\n");
	return result;
}






/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 1;
	char *user;
	struct passwd *pw;
	int option = 0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"metric", required_argument, 0, 'm'},
		{"timeout", required_argument, 0, 't'},
		{"status", required_argument, 0, 's'},
		{"ppid", required_argument, 0, 'p'},
		{"command", required_argument, 0, 'C'},
		{"vsz", required_argument, 0, 'z'},
		{"rss", required_argument, 0, 'r'},
		{"pcpu", required_argument, 0, 'P'},
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
		c = getopt_long (argc, argv, "Vvht:c:w:p:s:u:C:a:z:r:m:P:", 
			longopts, &option);

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
			print_revision (progname, revision);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage (_("Timeout Interval must be an integer!\n\n"));
			else
				timeout_interval = atoi (optarg);
			break;
		case 'c':									/* critical threshold */
			if (is_integer (optarg))
				cmax = atoi (optarg);
			else if (sscanf (optarg, ":%d", &cmax) == 1)
				break;
			else if (sscanf (optarg, "%d:%d", &cmin, &cmax) == 2)
				break;
			else if (sscanf (optarg, "%d:", &cmin) == 1)
				break;
			else
				usage (_("Critical Process Count must be an integer!\n\n"));
			break;							 
		case 'w':									/* warning threshold */
			if (is_integer (optarg))
				wmax = atoi (optarg);
			else if (sscanf (optarg, ":%d", &wmax) == 1)
				break;
			else if (sscanf (optarg, "%d:%d", &wmin, &wmax) == 2)
				break;
			else if (sscanf (optarg, "%d:", &wmin) == 1)
				break;
			else
				usage (_("Warning Process Count must be an integer!\n\n"));
			break;
		case 'p':									/* process id */
			if (sscanf (optarg, "%d%[^0-9]", &ppid, tmp) == 1) {
				asprintf (&fmt, "%s%sPPID = %d", (fmt ? fmt : "") , (options ? ", " : ""), ppid);
				options |= PPID;
				break;
			}
			usage2 (_("%s: Parent Process ID must be an integer!\n\n"),  progname);
		case 's':									/* status */
			if (statopts)
				break;
			else
				statopts = optarg;
			asprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
			options |= STAT;
			break;
		case 'u':									/* user or user id */
			if (is_integer (optarg)) {
				uid = atoi (optarg);
				pw = getpwuid ((uid_t) uid);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("UID %s was not found\n"), optarg);
			}
			else {
				pw = getpwnam (optarg);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("User name %s was not found\n"), optarg);
				/*  then get uid */
				uid = pw->pw_uid;
			}
			user = pw->pw_name;
			asprintf (&fmt, _("%s%sUID = %d (%s)"), (fmt ? fmt : ""), (options ? ", " : ""),
			          uid, user);
			options |= USER;
			break;
		case 'C':									/* command */
			if (prog)
				break;
			else
				prog = optarg;
			asprintf (&fmt, _("%s%scommand name '%s'"), (fmt ? fmt : ""), (options ? ", " : ""),
			          prog);
			options |= PROG;
			break;
		case 'a':									/* args (full path name with args) */
			if (args)
				break;
			else
				args = optarg;
			asprintf (&fmt, _("%s%sargs '%s'"), (fmt ? fmt : ""), (options ? ", " : ""), args);
			options |= ARGS;
			break;
		case 'r': 					/* RSS */
			if (sscanf (optarg, "%d%[^0-9]", &rss, tmp) == 1) {
				asprintf (&fmt, _("%s%sRSS >= %d"), (fmt ? fmt : ""), (options ? ", " : ""), rss);
				options |= RSS;
				break;
			}
			usage2 (_("%s: RSS must be an integer!\n\n"), progname);
		case 'z':					/* VSZ */
			if (sscanf (optarg, "%d%[^0-9]", &vsz, tmp) == 1) {
				asprintf (&fmt, _("%s%sVSZ >= %d"), (fmt ? fmt : ""), (options ? ", " : ""), vsz);
				options |= VSZ;
				break;
			}
			usage2 (_("%s: VSZ must be an integer!\n\n"), progname);
		case 'P':					/* PCPU */
			/* TODO: -P 1.5.5 is accepted */
			if (sscanf (optarg, "%f%[^0-9.]", &pcpu, tmp) == 1) {
				asprintf (&fmt, _("%s%sPCPU >= %.2f"), (fmt ? fmt : ""), (options ? ", " : ""), pcpu);
				options |= PCPU;
				break;
			}
			usage2 (_("%s: PCPU must be a float!\n\n"), progname);
		case 'm':
			asprintf (&metric_name, "%s", optarg);
			if ( strcmp(optarg, "PROCS") == 0) {
				metric = METRIC_PROCS;
				break;
			} 
			else if ( strcmp(optarg, "VSZ") == 0) {
				metric = METRIC_VSZ;
				break;
			} 
			else if ( strcmp(optarg, "RSS") == 0 ) {
				metric = METRIC_RSS;
				break;
			}
			else if ( strcmp(optarg, "CPU") == 0 ) {
				metric = METRIC_CPU;
				break;
			}
			printf (_("%s: metric must be one of PROCS, VSZ, RSS, CPU!\n\n"),
				progname);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'v':									/* command */
			verbose++;
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
		asprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
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
			printf (_("wmax (%d) cannot be greater than cmax (%d)\n"), wmax, cmax);
			return ERROR;
		}
		if (cmin > wmin && wmin != -1) {
			printf (_("wmin (%d) cannot be less than cmin (%d)\n"), wmin, cmin);
			return ERROR;
		}
	}

/* 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) { */
/* 		printf ("At least one threshold must be set\n"); */
/* 		return ERROR; */
/* 	} */

	if (options == 0)
		options = ALL;

	if (statopts==NULL)
		statopts = strdup("");

	if (prog==NULL)
		prog = strdup("");

	if (args==NULL)
		args = strdup("");

	if (fmt==NULL)
		fmt = strdup("");

	if (fails==NULL)
		fails = strdup("");

	return options;
}






/* Check thresholds against value */
int
check_thresholds (int value)
{
 	if (wmax == -1 && cmax == -1 && wmin == -1 && cmin == -1) {
		return OK;
 	}
	else if (cmax >= 0 && cmin >= 0 && cmax < cmin) {
		if (value > cmax && value < cmin)
			return STATE_CRITICAL;
	}
	else if (cmax >= 0 && value > cmax) {
		return STATE_CRITICAL;
	}
	else if (cmin >= 0 && value < cmin) {
		return STATE_CRITICAL;
	}

	if (wmax >= 0 && wmin >= 0 && wmax < wmin) {
		if (value > wmax && value < wmin) {
			return STATE_WARNING;
		}
	}
	else if (wmax >= 0 && value > wmax) {
		return STATE_WARNING;
	}
	else if (wmin >= 0 && value < wmin) {
		return STATE_WARNING;
	}
	return STATE_OK;
}






void
print_help (void)
{
	print_revision (progname, revision);

	printf (_("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>"));
	printf (_(COPYRIGHT), copyright, email);

	printf(_("\
Checks all processes and generates WARNING or CRITICAL states if the specified\n\
metric is outside the required threshold ranges. The metric defaults to number\n\
of processes.  Search filters can be applied to limit the processes to check.\n\n"));

	print_usage ();

	printf(_("\n\
Required Arguments:\n\
 -w, --warning=RANGE\n\
   Generate warning state if metric is outside this range\n\
 -c, --critical=RANGE\n\
   Generate critical state if metric is outside this range\n"));

	printf(_("\n\
Optional Arguments:\n\
 -m, --metric=TYPE\n\
   Check thresholds against metric. Valid types:\n\
   PROCS - number of processes (default)\n\
   VSZ  - virtual memory size\n\
   RSS  - resident set memory size\n\
   CPU  - percentage cpu\n\
 -v, --verbose\n\
   Extra information. Up to 3 verbosity levels\n"));

	printf(_("\n\
Optional Filters:\n\
 -s, --state=STATUSFLAGS\n\
   Only scan for processes that have, in the output of `ps`, one or\n\
   more of the status flags you specify (for example R, Z, S, RS,\n\
   RSZDT, plus others based on the output of your 'ps' command).\n\
 -p, --ppid=PPID\n\
   Only scan for children of the parent process ID indicated.\n\
 -z, --vsz=VSZ\n\
   Only scan for processes with vsz higher than indicated.\n\
 -r, --rss=RSS\n\
   Only scan for processes with rss higher than indicated.\n"));

	printf(_("\
 -P, --pcpu=PCPU\n\
   Only scan for processes with pcpu higher than indicated.\n\
 -u, --user=USER\n\
   Only scan for processes with user name or ID indicated.\n\
 -a, --argument-array=STRING\n\
   Only scan for processes with args that contain STRING.\n\
 -C, --command=COMMAND\n\
   Only scan for exact matches of COMMAND (without path).\n"));

	printf(_("\n\
RANGEs are specified 'min:max' or 'min:' or ':max' (or 'max'). If\n\
specified 'max:min', a warning status will be generated if the\n\
count is inside the specified range\n\n"));

	printf(_("\
This plugin checks the number of currently running processes and\n\
generates WARNING or CRITICAL states if the process count is outside\n\
the specified threshold ranges. The process count can be filtered by\n\
process owner, parent process PID, current state (e.g., 'Z'), or may\n\
be the total number of running processes\n\n"));

	printf(_("\
Examples:\n\
 check_procs -w 2:2 -c 2:1024 -C portsentry\n\
   Warning if not two processes with command name portsentry. Critical\n\
   if < 2 or > 1024 processes\n\n\
 check_procs -w 10 -a '/usr/local/bin/perl' -u root\n\
   Warning alert if > 10 processes with command arguments containing \n\
   '/usr/local/bin/perl' and owned by root\n\n\
 check_procs -w 50000 -c 100000 --metric=VSZ\n\
   Alert if vsz of any processes over 50K or 100K\n\
 check_procs -w 10 -c 20 --metric=CPU\n\
   Alert if cpu of any processes over 10% or 20%\n\n"));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
	printf ("\
Usage: %s -w <range> -c <range> [-m metric] [-s state] [-p ppid]\n\
  [-u user] [-r rss] [-z vsz] [-P %%cpu] [-a argument-array]\n\
  [-C command] [-v]\n", progname);	
	printf (_(UT_HLP_VRS), progname, progname);
}

