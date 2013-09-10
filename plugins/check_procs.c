/*****************************************************************************
* 
* Nagios check_procs plugin
* 
* License: GPL
* Copyright (c) 2000-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_procs plugin
* 
* Checks all processes and generates WARNING or CRITICAL states if the
* specified metric is outside the required threshold ranges. The metric
* defaults to number of processes.  Search filters can be applied to limit
* the processes to check.
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

const char *progname = "check_procs";
const char *program_name = "check_procs";  /* Required for coreutils libs */
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"
#include "regex.h"

#include <pwd.h>
#include <errno.h>

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

int process_arguments (int, char **);
int validate_arguments (void);
int convert_to_seconds (char *); 
void print_help (void);
void print_usage (void);

char *warning_range = NULL;
char *critical_range = NULL;
thresholds *procs_thresholds = NULL;

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
#define ELAPSED 512
#define EREG_ARGS 1024

#define KTHREAD_PARENT "kthreadd" /* the parent process of kernel threads:
							ppid of procs are compared to pid of this proc*/

/* Different metrics */
char *metric_name;
enum metric {
	METRIC_PROCS,
	METRIC_VSZ,
	METRIC_RSS,
	METRIC_CPU,
	METRIC_ELAPSED
};
enum metric metric = METRIC_PROCS;

int verbose = 0;
int uid;
pid_t ppid;
int vsz;
int rss;
float pcpu;
char *statopts;
char *prog;
char *args;
char *input_filename = NULL;
regex_t re_args;
char *fmt;
char *fails;
char tmp[MAX_INPUT_BUFFER];
int kthread_filter = 0;
int usepid = 0; /* whether to test for pid or /proc/pid/exe */

FILE *ps_input = NULL;

static int
stat_exe (const pid_t pid, struct stat *buf) {
	char *path;
	int ret;
	xasprintf(&path, "/proc/%d/exe", pid);
	ret = stat(path, buf);
	free(path);
	return ret;
}


int
main (int argc, char **argv)
{
	char *input_buffer;
	char *input_line;
	char *procprog;

	pid_t mypid = 0;
	struct stat statbuf;
	dev_t mydev = 0;
	ino_t myino = 0;
	int procuid = 0;
	pid_t procpid = 0;
	pid_t procppid = 0;
	pid_t kthread_ppid = 0;
	int procvsz = 0;
	int procrss = 0;
	int procseconds = 0;
	float procpcpu = 0;
	char procstat[8];
	char procetime[MAX_INPUT_BUFFER] = { '\0' };
	char *procargs;

	const char *zombie = "Z";

	int resultsum = 0; /* bitmask of the filter criteria met by a process */
	int found = 0; /* counter for number of lines returned in `ps` output */
	int procs = 0; /* counter for number of processes meeting filter criteria */
	int pos; /* number of spaces before 'args' in `ps` output */
	int cols; /* number of columns in ps output */
	int expected_cols = PS_COLS - 1;
	int warn = 0; /* number of processes in warn state */
	int crit = 0; /* number of processes in crit state */
	int i = 0, j = 0;
	int result = STATE_UNKNOWN;
	int ret = 0;
	output chld_out, chld_err;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	setlocale(LC_NUMERIC, "POSIX");

	input_buffer = malloc (MAX_INPUT_BUFFER);
	procprog = malloc (MAX_INPUT_BUFFER);

	xasprintf (&metric_name, "PROCS");
	metric = METRIC_PROCS;

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* find ourself */
	mypid = getpid();
	if (usepid || stat_exe(mypid, &statbuf) == -1) {
		/* usepid might have been set by -T */
		usepid = 1;
	} else {
		usepid = 0;
		mydev = statbuf.st_dev;
		myino = statbuf.st_ino;
	}

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		die (STATE_UNKNOWN, _("Cannot catch SIGALRM"));
	}
	(void) alarm ((unsigned) timeout_interval);

	if (verbose >= 2)
		printf (_("CMD: %s\n"), PS_COMMAND);

	if (input_filename == NULL) {
		result = cmd_run( PS_COMMAND, &chld_out, &chld_err, 0);
		if (chld_err.lines > 0) {
			printf ("%s: %s", _("System call sent warnings to stderr"), chld_err.line[0]);
			exit(STATE_WARNING);
		}
	} else {
		result = cmd_file_read( input_filename, &chld_out, 0);
	}

	/* flush first line: j starts at 1 */
	for (j = 1; j < chld_out.lines; j++) {
		input_line = chld_out.line[j];

		if (verbose >= 3)
			printf ("%s", input_line);

		strcpy (procprog, "");
		xasprintf (&procargs, "%s", "");

		cols = sscanf (input_line, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		if ( cols < expected_cols && strstr(procstat, zombie) ) {
			cols = expected_cols;
		}
		if ( cols >= expected_cols ) {
			resultsum = 0;
			xasprintf (&procargs, "%s", input_line + pos);
			strip (procargs);

			/* Some ps return full pathname for command. This removes path */
			strcpy(procprog, base_name(procprog));

			/* we need to convert the elapsed time to seconds */
			procseconds = convert_to_seconds(procetime);

			if (verbose >= 3)
				printf ("proc#=%d uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s prog=%s args=%s\n", 
					procs, procuid, procvsz, procrss,
					procpid, procppid, procpcpu, procstat, 
					procetime, procprog, procargs);

			/* Ignore self */
			if ((usepid && mypid == procpid) ||
				(!usepid && ((ret = stat_exe(procpid, &statbuf) != -1) && statbuf.st_dev == mydev && statbuf.st_ino == myino) ||
				 (ret == -1 && errno == ENOENT))) {
				if (verbose >= 3)
					 printf("not considering - is myself or gone\n");
				continue;
			}

			/* filter kernel threads (childs of KTHREAD_PARENT)*/
			/* TODO adapt for other OSes than GNU/Linux
					sorry for not doing that, but I've no other OSes to test :-( */
			if (kthread_filter == 1) {
				/* get pid KTHREAD_PARENT */
				if (kthread_ppid == 0 && !strcmp(procprog, KTHREAD_PARENT) )
					kthread_ppid = procpid;

				if (kthread_ppid == procppid) {
					if (verbose >= 2)
						printf ("Ignore kernel thread: pid=%d ppid=%d prog=%s args=%s\n", procpid, procppid, procprog, procargs);
					continue;
				}
			}

			if ((options & STAT) && (strstr (statopts, procstat)))
				resultsum |= STAT;
			if ((options & ARGS) && procargs && (strstr (procargs, args) != NULL))
				resultsum |= ARGS;
			if ((options & EREG_ARGS) && procargs && (regexec(&re_args, procargs, (size_t) 0, NULL, 0) == 0))
				resultsum |= EREG_ARGS;
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
			if (verbose >= 2) {
				printf ("Matched: uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s prog=%s args=%s\n", 
					procuid, procvsz, procrss,
					procpid, procppid, procpcpu, procstat, 
					procetime, procprog, procargs);
			}

			if (metric == METRIC_VSZ)
				i = get_status ((double)procvsz, procs_thresholds);
			else if (metric == METRIC_RSS)
				i = get_status ((double)procrss, procs_thresholds);
			/* TODO? float thresholds for --metric=CPU */
			else if (metric == METRIC_CPU)
				i = get_status (procpcpu, procs_thresholds);
			else if (metric == METRIC_ELAPSED)
				i = get_status ((double)procseconds, procs_thresholds);

			if (metric != METRIC_PROCS) {
				if (i == STATE_WARNING) {
					warn++;
					xasprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
				if (i == STATE_CRITICAL) {
					crit++;
					xasprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
					result = max_state (result, i);
				}
			}
		} 
		/* This should not happen */
		else if (verbose) {
			printf(_("Not parseable: %s"), input_buffer);
		}
	}

	if (found == 0) {							/* no process lines parsed so return STATE_UNKNOWN */
		printf (_("Unable to read output\n"));
		return STATE_UNKNOWN;
	}

	if ( result == STATE_UNKNOWN ) 
		result = STATE_OK;

	/* Needed if procs found, but none match filter */
	if ( metric == METRIC_PROCS ) {
		result = max_state (result, get_status ((double)procs, procs_thresholds) );
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

	if (metric == METRIC_PROCS)
		printf (" | procs=%d;%s;%s;0;", procs,
				warning_range ? warning_range : "",
				critical_range ? critical_range : "");
	else
		printf (" | procs=%d;;;0; procs_warn=%d;;;0; procs_crit=%d;;;0;", procs, warn, crit);

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
	int err;
	int cflags = REG_NOSUB | REG_EXTENDED;
	char errbuf[MAX_INPUT_BUFFER];
	char *temp_string;
	int i=0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"metric", required_argument, 0, 'm'},
		{"timeout", required_argument, 0, 't'},
		{"status", required_argument, 0, 's'},
		{"ppid", required_argument, 0, 'p'},
		{"user", required_argument, 0, 'u'},
		{"command", required_argument, 0, 'C'},
		{"vsz", required_argument, 0, 'z'},
		{"rss", required_argument, 0, 'r'},
		{"pcpu", required_argument, 0, 'P'},
		{"elapsed", required_argument, 0, 'e'},
		{"argument-array", required_argument, 0, 'a'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"ereg-argument-array", required_argument, 0, CHAR_MAX+1},
		{"input-file", required_argument, 0, CHAR_MAX+2},
		{"no-kthreads", required_argument, 0, 'k'},
		{"traditional-filter", no_argument, 0, 'T'},
		{0, 0, 0, 0}
	};

	for (c = 1; c < argc; c++)
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");

	while (1) {
		c = getopt_long (argc, argv, "Vvhkt:c:w:p:s:u:C:a:z:r:m:P:T",
			longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?':									/* help */
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
			else
				timeout_interval = atoi (optarg);
			break;
		case 'c':									/* critical threshold */
			critical_range = optarg;
			break;							 
		case 'w':									/* warning threshold */
			warning_range = optarg;
			break;
		case 'p':									/* process id */
			if (sscanf (optarg, "%d%[^0-9]", &ppid, tmp) == 1) {
				xasprintf (&fmt, "%s%sPPID = %d", (fmt ? fmt : "") , (options ? ", " : ""), ppid);
				options |= PPID;
				break;
			}
			usage4 (_("Parent Process ID must be an integer!"));
		case 's':									/* status */
			if (statopts)
				break;
			else
				statopts = optarg;
			xasprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
			options |= STAT;
			break;
		case 'u':									/* user or user id */
			if (is_integer (optarg)) {
				uid = atoi (optarg);
				pw = getpwuid ((uid_t) uid);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("UID was not found"), optarg);
			}
			else {
				pw = getpwnam (optarg);
				/*  check to be sure user exists */
				if (pw == NULL)
					usage2 (_("User name was not found"), optarg);
				/*  then get uid */
				uid = pw->pw_uid;
			}
			user = pw->pw_name;
			xasprintf (&fmt, "%s%sUID = %d (%s)", (fmt ? fmt : ""), (options ? ", " : ""),
			          uid, user);
			options |= USER;
			break;
		case 'C':									/* command */
			/* TODO: allow this to be passed in with --metric */
			if (prog)
				break;
			else
				prog = optarg;
			xasprintf (&fmt, _("%s%scommand name '%s'"), (fmt ? fmt : ""), (options ? ", " : ""),
			          prog);
			options |= PROG;
			break;
		case 'a':									/* args (full path name with args) */
			/* TODO: allow this to be passed in with --metric */
			if (args)
				break;
			else
				args = optarg;
			xasprintf (&fmt, "%s%sargs '%s'", (fmt ? fmt : ""), (options ? ", " : ""), args);
			options |= ARGS;
			break;
		case CHAR_MAX+1:
			err = regcomp(&re_args, optarg, cflags);
			if (err != 0) {
				regerror (err, &re_args, errbuf, MAX_INPUT_BUFFER);
				die (STATE_UNKNOWN, "PROCS %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
			}
			/* Strip off any | within the regex optarg */
			temp_string = strdup(optarg);
			while(temp_string[i]!='\0'){
				if(temp_string[i]=='|')
					temp_string[i]=',';
				i++;
			}
			xasprintf (&fmt, "%s%sregex args '%s'", (fmt ? fmt : ""), (options ? ", " : ""), temp_string);
			options |= EREG_ARGS;
			break;
		case 'r': 					/* RSS */
			if (sscanf (optarg, "%d%[^0-9]", &rss, tmp) == 1) {
				xasprintf (&fmt, "%s%sRSS >= %d", (fmt ? fmt : ""), (options ? ", " : ""), rss);
				options |= RSS;
				break;
			}
			usage4 (_("RSS must be an integer!"));
		case 'z':					/* VSZ */
			if (sscanf (optarg, "%d%[^0-9]", &vsz, tmp) == 1) {
				xasprintf (&fmt, "%s%sVSZ >= %d", (fmt ? fmt : ""), (options ? ", " : ""), vsz);
				options |= VSZ;
				break;
			}
			usage4 (_("VSZ must be an integer!"));
		case 'P':					/* PCPU */
			/* TODO: -P 1.5.5 is accepted */
			if (sscanf (optarg, "%f%[^0-9.]", &pcpu, tmp) == 1) {
				xasprintf (&fmt, "%s%sPCPU >= %.2f", (fmt ? fmt : ""), (options ? ", " : ""), pcpu);
				options |= PCPU;
				break;
			}
			usage4 (_("PCPU must be a float!"));
		case 'm':
			xasprintf (&metric_name, "%s", optarg);
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
			else if ( strcmp(optarg, "ELAPSED") == 0) {
				metric = METRIC_ELAPSED;
				break;
			}
				
			usage4 (_("Metric must be one of PROCS, VSZ, RSS, CPU, ELAPSED!"));
		case 'k':	/* linux kernel thread filter */
			kthread_filter = 1;
			break;
		case 'v':									/* command */
			verbose++;
			break;
		case 'T':
			usepid = 1;
			break;
		case CHAR_MAX+2:
			input_filename = optarg;
			break;
		}
	}

	c = optind;
	if ((! warning_range) && argv[c])
		warning_range = argv[c++];
	if ((! critical_range) && argv[c])
		critical_range = argv[c++];
	if (statopts == NULL && argv[c]) {
		xasprintf (&statopts, "%s", argv[c++]);
		xasprintf (&fmt, _("%s%sSTATE = %s"), (fmt ? fmt : ""), (options ? ", " : ""), statopts);
		options |= STAT;
	}

	/* this will abort in case of invalid ranges */
	set_thresholds (&procs_thresholds, warning_range, critical_range);

	return validate_arguments ();
}



int
validate_arguments ()
{
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


/* convert the elapsed time to seconds */
int
convert_to_seconds(char *etime) {

	char *ptr;
	int total;

	int hyphcnt;
	int coloncnt;
	int days;
	int hours;
	int minutes;
	int seconds;

	hyphcnt = 0;
	coloncnt = 0;
	days = 0;
	hours = 0;
	minutes = 0;
	seconds = 0;

	for (ptr = etime; *ptr != '\0'; ptr++) {
	
		if (*ptr == '-') {
			hyphcnt++;
			continue;
		}
		if (*ptr == ':') {
			coloncnt++;
			continue;
		}
	}

	if (hyphcnt > 0) {
		sscanf(etime, "%d-%d:%d:%d",
				&days, &hours, &minutes, &seconds);
		/* linux 2.6.5/2.6.6 reporting some processes with infinite
		 * elapsed times for some reason */
		if (days == 49710) {
			return 0;
		}
	} else {
		if (coloncnt == 2) {
			sscanf(etime, "%d:%d:%d",
				&hours, &minutes, &seconds);
		} else if (coloncnt == 1) {
			sscanf(etime, "%d:%d",
				&minutes, &seconds);
		}
	}

	total = (days * 86400) +
		(hours * 3600) +
		(minutes * 60) +
		seconds;

	if (verbose >= 3 && metric == METRIC_ELAPSED) {
			printf("seconds: %d\n", total);
	}
	return total;
}


void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("Checks all processes and generates WARNING or CRITICAL states if the specified"));
  printf ("%s\n", _("metric is outside the required threshold ranges. The metric defaults to number"));
  printf ("%s\n", _("of processes.  Search filters can be applied to limit the processes to check."));

  printf ("\n\n");

	print_usage ();

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);
  printf (" %s\n", "-w, --warning=RANGE");
  printf ("   %s\n", _("Generate warning state if metric is outside this range"));
  printf (" %s\n", "-c, --critical=RANGE");
  printf ("   %s\n", _("Generate critical state if metric is outside this range"));
  printf (" %s\n", "-m, --metric=TYPE");
  printf ("  %s\n", _("Check thresholds against metric. Valid types:"));
  printf ("  %s\n", _("PROCS   - number of processes (default)"));
  printf ("  %s\n", _("VSZ     - virtual memory size"));
  printf ("  %s\n", _("RSS     - resident set memory size"));
  printf ("  %s\n", _("CPU     - percentage CPU"));
/* only linux etime is support currently */
#if defined( __linux__ )
	printf ("  %s\n", _("ELAPSED - time elapsed in seconds"));
#endif /* defined(__linux__) */
	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (" %s\n", "-v, --verbose");
  printf ("    %s\n", _("Extra information. Up to 3 verbosity levels"));

  printf (" %s\n", "-T, --traditional");
  printf ("   %s\n", _("Filter own process the traditional way by PID instead of /proc/pid/exe"));

  printf ("\n");
	printf ("%s\n", "Filters:");
  printf (" %s\n", "-s, --state=STATUSFLAGS");
  printf ("   %s\n", _("Only scan for processes that have, in the output of `ps`, one or"));
  printf ("   %s\n", _("more of the status flags you specify (for example R, Z, S, RS,"));
  printf ("   %s\n", _("RSZDT, plus others based on the output of your 'ps' command)."));
  printf (" %s\n", "-p, --ppid=PPID");
  printf ("   %s\n", _("Only scan for children of the parent process ID indicated."));
  printf (" %s\n", "-z, --vsz=VSZ");
  printf ("   %s\n", _("Only scan for processes with VSZ higher than indicated."));
  printf (" %s\n", "-r, --rss=RSS");
  printf ("   %s\n", _("Only scan for processes with RSS higher than indicated."));
	printf (" %s\n", "-P, --pcpu=PCPU");
  printf ("   %s\n", _("Only scan for processes with PCPU higher than indicated."));
  printf (" %s\n", "-u, --user=USER");
  printf ("   %s\n", _("Only scan for processes with user name or ID indicated."));
  printf (" %s\n", "-a, --argument-array=STRING");
  printf ("   %s\n", _("Only scan for processes with args that contain STRING."));
  printf (" %s\n", "--ereg-argument-array=STRING");
  printf ("   %s\n", _("Only scan for processes with args that contain the regex STRING."));
  printf (" %s\n", "-C, --command=COMMAND");
  printf ("   %s\n", _("Only scan for exact matches of COMMAND (without path)."));
  printf (" %s\n", "-k, --no-kthreads");
  printf ("   %s\n", _("Only scan for non kernel threads (works on Linux only)."));

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

	printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_procs -w 2:2 -c 2:1024 -C portsentry");
  printf ("  %s\n", _("Warning if not two processes with command name portsentry."));
  printf ("  %s\n\n", _("Critical if < 2 or > 1024 processes"));
  printf (" %s\n", "check_procs -w 10 -a '/usr/local/bin/perl' -u root");
  printf ("  %s\n", _("Warning alert if > 10 processes with command arguments containing"));
  printf ("  %s\n\n", _("'/usr/local/bin/perl' and owned by root"));
  printf (" %s\n", "check_procs -w 50000 -c 100000 --metric=VSZ");
  printf ("  %s\n\n", _("Alert if VSZ of any processes over 50K or 100K"));
  printf (" %s\n", "check_procs -w 10 -c 20 --metric=CPU");
  printf ("  %s\n", _("Alert if CPU of any processes over 10%% or 20%%"));

	printf (UT_SUPPORT);
}

void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
	printf ("%s -w <range> -c <range> [-m metric] [-s state] [-p ppid]\n", progname);
  printf (" [-u user] [-r rss] [-z vsz] [-P %%cpu] [-a argument-array]\n");
  printf (" [-C command] [-k] [-t timeout] [-v]\n");
}
