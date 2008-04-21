/*****************************************************************************
* 
* Nagios check_procs plugin
* 
* License: GPL
* Copyright (c) 2000-2008 Nagios Plugins Development Team
* 
* Last Modified: $Date$
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
* $Id$
* 
*****************************************************************************/

const char *progname = "check_procs";
const char *program_name = "check_procs";  /* Required for coreutils libs */
const char *revision = "$Revision$";
const char *copyright = "2000-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"
#include "regex.h"
#include "utils_base.h"

#include <pwd.h>

int process_arguments (int, char **);
int validate_arguments (void);
int check_thresholds (int);
int convert_to_seconds (char *); 
void print_help (void);
void print_usage (void);
void actions_on_failed_state (int, char*);	/* Helper routine */

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
#define ELAPSED 512
#define EREG_ARGS 1024
/* Different metrics */
char *metric_name;
enum metric {
	NONE,
	DEFAULT,
	METRIC_PROCS,
	METRIC_VSZ,
	METRIC_RSS,
	METRIC_CPU,
	METRIC_ELAPSED
};
enum metric metric = DEFAULT;
enum metric default_metric = METRIC_PROCS;

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

FILE *ps_input = NULL;

thresholds *number_threshold = NULL;
thresholds *vsz_threshold = NULL;
thresholds *rss_threshold = NULL;
thresholds *cpu_threshold = NULL;
int new_style_thresholds = 0;

int warn = 0; /* number of processes in warn state */
int crit = 0; /* number of processes in crit state */
int result = STATE_UNKNOWN;

int
main (int argc, char **argv)
{
	char *input_buffer;
	char *input_line;
	char *procprog;
	char *last_critical = NULL;
	char *last_warning = NULL;
	char *last_failed_process = NULL;

	pid_t mypid = 0;
	int procuid = 0;
	pid_t procpid = 0;
	pid_t procppid = 0;
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
	int i = 0, j = 0;	/* Temporary values */
	double rss_sum = 0;
	double vsz_sum = 0;
	double cpu_sum = 0;
	double vsz_max = 0;
	double rss_max = 0;
	double cpu_max = 0;
	int multiple_process_output_flag = 0;
	int number_threshold_failure_flag = 0;
	output chld_out, chld_err;


	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	setlocale(LC_NUMERIC, "POSIX");

	input_buffer = malloc (MAX_INPUT_BUFFER);
	procprog = malloc (MAX_INPUT_BUFFER);

	asprintf (&metric_name, "PROCS");
	asprintf (&last_failed_process, "");

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* get our pid */
	mypid = getpid();

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		die (STATE_UNKNOWN, _("Cannot catch SIGALRM"));
	}
	(void) alarm ((unsigned) timeout_interval);

	if (verbose >= 3)
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
		asprintf (&procargs, "%s", "");

		cols = sscanf (input_line, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		if ( cols < expected_cols && strstr(procstat, zombie) ) {
			cols = expected_cols;
		}
		if ( cols >= expected_cols ) {
			resultsum = 0;
			asprintf (&procargs, "%s", input_line + pos);
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
			if (mypid == procpid) continue;

			/* Filter */
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
			if (verbose >= 3) {
				printf ("Matched: uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s prog=%s args=%s\n", 
					procuid, procvsz, procrss,
					procpid, procppid, procpcpu, procstat, 
					procetime, procprog, procargs);
			}

			/* Check against metric - old style single check */
			if (metric == METRIC_VSZ) {
				actions_on_failed_state( check_thresholds (procvsz), procprog );
			} else if (metric == METRIC_RSS) {
				actions_on_failed_state( check_thresholds (procrss), procprog );
			/* TODO? float thresholds for --metric=CPU */
			} else if (metric == METRIC_CPU) {
				actions_on_failed_state( check_thresholds ((int)procpcpu), procprog ); 
			} else if (metric == METRIC_ELAPSED) {
				actions_on_failed_state( check_thresholds (procseconds), procprog );
			}

			asprintf( &last_critical, "" );
			asprintf( &last_warning, "" );
			/* Check against all new style thresholds */
			if (vsz_threshold != NULL) {
				if ((i = get_status( procvsz, vsz_threshold )) != STATE_OK ) {
					actions_on_failed_state(i, procprog);
					asprintf( &last_failed_process, "%s", procprog );
					if (i == STATE_CRITICAL) {
						asprintf( &last_critical, "vsz=%d", procvsz );
					} else if (i == STATE_WARNING) {
						asprintf( &last_warning, "vsz=%d", procvsz );
					}
					if (verbose >= 2) {
						printf("VSZ state %d: proc=%s vsz=%d ", i, procprog, procvsz);
						print_thresholds( vsz_threshold );
					}
				}
			}
			if (rss_threshold != NULL) {
				if ((i = get_status( procrss, rss_threshold )) != STATE_OK ) {
					actions_on_failed_state(i, procprog);
					asprintf( &last_failed_process, "%s", procprog );
					if (i == STATE_CRITICAL) {
						asprintf( &last_critical, "%s%srss=%d", last_critical, (strcmp(last_critical,"") ? ", " : ""), procrss );
					} else if (i == STATE_WARNING) {
						asprintf( &last_warning, "%s%srss=%d", last_warning, (strcmp(last_warning,"") ? ", " : ""), procrss );
					}
					if (verbose >= 2) {
						printf("RSS: proc=%s rss=%d ", procprog, procrss);
						print_thresholds( rss_threshold );
					}
				}
			}
			if (cpu_threshold != NULL) {
				if (( i = get_status( procpcpu, cpu_threshold )) != STATE_OK ) {
					actions_on_failed_state(i, procprog);
					asprintf( &last_failed_process, "%s", procprog );
					if (i == STATE_CRITICAL) {
						asprintf( &last_critical, "%s%scpu=%.2f%%", last_critical, (strcmp(last_critical,"") ? ", " : ""), procpcpu );
					} else if (i == STATE_WARNING) {
						asprintf( &last_warning, "%s%scpu=%.2f%%", last_warning, (strcmp(last_warning,"") ? ", " : ""), procpcpu );
					}
					if (verbose >= 2) {
						printf("CPU: proc=%s cpu=%f ", procprog, procpcpu);
						print_thresholds( cpu_threshold );
					}
				}
			}
		
			/* Summary information */
			rss_sum += procrss;
			vsz_sum += procvsz;
			cpu_sum += procpcpu;
			if (procrss > rss_max)
				rss_max = procrss;
			if (procvsz > vsz_max)
				vsz_max = procvsz;
			if (procpcpu > cpu_max)
				cpu_max = procpcpu;
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
		result = max_state (result, i = check_thresholds (procs) );
	}

	if (number_threshold != NULL) {
		if ((i = get_status( procs, number_threshold )) != STATE_OK) {
			actions_on_failed_state(i, "NUMBER_OF_PROCESSES");
			if (verbose >= 2) {
				printf("NUMBER: total_procs=%d ", procs);
				print_thresholds( number_threshold );
			}
			number_threshold_failure_flag = 1;
		}
	}

	if ( result == STATE_OK ) {
		printf ("%s %s: ", metric_name, _("OK"));
		multiple_process_output_flag = 1;
	} else if (result == STATE_WARNING) {
		printf ("%s %s: ", metric_name, _("WARNING"));
		if (procs == 1 && new_style_thresholds && ! number_threshold_failure_flag) {
			printf("%s: warning %s", last_failed_process, last_warning);
		} else {
			if ( metric != METRIC_PROCS ) {
				printf (_("Alerts: %d warn from "), warn);
			}
			multiple_process_output_flag = 1;
		}
	} else if (result == STATE_CRITICAL) {
		printf ("%s %s: ", metric_name, _("CRITICAL"));
		if (procs == 1 && new_style_thresholds && ! number_threshold_failure_flag) {
			printf("%s: critical %s", last_failed_process, last_critical);
			if (strcmp(last_warning, "")) {
				printf("; warning %s", last_warning);
			}
		} else {
			if (metric != METRIC_PROCS) {
				printf (_("Alerts: %d crit, %d warn from "), crit, warn);
			}
			multiple_process_output_flag = 1;
		}
	} 

	if (multiple_process_output_flag) {
		printf (ngettext ("%d process", "%d processes", (unsigned long) procs), procs);
	
		if (strcmp(fmt,"") != 0) {
			printf (_(" with %s"), fmt);
		}

		if ( verbose >= 1 && strcmp(fails,"") )
			printf (" [%s]", fails);
	}

	printf(" | ");
	if( number_threshold != NULL)
		printf("number=%d ", procs);
	if (procs > 0) {
		if( vsz_threshold != NULL)
			printf("vsz=%.0f ", vsz_sum/procs);
		if( rss_threshold != NULL)
			printf("rss=%.0f ", rss_sum/procs);
		if( cpu_threshold != NULL)
			printf("cpu=%.2f ", cpu_sum/procs);
	}
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
		{"elapsed", required_argument, 0, 'e'},
		{"argument-array", required_argument, 0, 'a'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{"verbose", no_argument, 0, 'v'},
		{"ereg-argument-array", required_argument, 0, CHAR_MAX+1},
		{"input-file", required_argument, 0, CHAR_MAX+2},
		{"number", optional_argument, 0, CHAR_MAX+3},
		{"rss-threshold", optional_argument, 0, CHAR_MAX+4},
		/*
		{"rss-max", optional_argument, 0, CHAR_MAX+5},
		{"rss-sum", optional_argument, 0, CHAR_MAX+6},
		*/
		{"vsz-threshold", optional_argument, 0, CHAR_MAX+7},
		/*
		{"vsz-max", optional_argument, 0, CHAR_MAX+8},
		{"vsz-sum", optional_argument, 0, CHAR_MAX+9},
		*/
		{"cpu-threshold", optional_argument, 0, CHAR_MAX+10},
		/*
		{"cpu-max", optional_argument, 0, CHAR_MAX+11},
		{"cpu-sum", optional_argument, 0, CHAR_MAX+12},
		*/
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
			usage5 ();
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 't':									/* timeout period */
			if (!is_integer (optarg))
				usage2 (_("Timeout interval must be a positive integer"), optarg);
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
				usage4 (_("Critical Process Count must be an integer!"));
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
				usage4 (_("Warning Process Count must be an integer!"));
			break;
		case 'p':									/* process id */
			if (sscanf (optarg, "%d%[^0-9]", &ppid, tmp) == 1) {
				asprintf (&fmt, "%s%sPPID = %d", (fmt ? fmt : "") , (options ? ", " : ""), ppid);
				options |= PPID;
				break;
			}
			usage4 (_("Parent Process ID must be an integer!"));
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
			asprintf (&fmt, "%s%sUID = %d (%s)", (fmt ? fmt : ""), (options ? ", " : ""),
			          uid, user);
			options |= USER;
			break;
		case 'C':									/* command */
			/* TODO: allow this to be passed in with --metric */
			if (prog)
				break;
			else
				prog = optarg;
			asprintf (&fmt, _("%s%scommand name '%s'"), (fmt ? fmt : ""), (options ? ", " : ""),
			          prog);
			options |= PROG;
			break;
		case 'a':									/* args (full path name with args) */
			/* TODO: allow this to be passed in with --metric */
			if (args)
				break;
			else
				args = optarg;
			asprintf (&fmt, "%s%sargs '%s'", (fmt ? fmt : ""), (options ? ", " : ""), args);
			options |= ARGS;
			break;
		case CHAR_MAX+1:
			err = regcomp(&re_args, optarg, cflags);
			if (err != 0) {
				regerror (err, &re_args, errbuf, MAX_INPUT_BUFFER);
				die (STATE_UNKNOWN, "PROCS %s: %s - %s\n", _("UNKNOWN"), _("Could not compile regular expression"), errbuf);
			}
			asprintf (&fmt, "%s%sregex args '%s'", (fmt ? fmt : ""), (options ? ", " : ""), optarg);
			options |= EREG_ARGS;
			break;
		case 'r': 					/* RSS */
			if (sscanf (optarg, "%d%[^0-9]", &rss, tmp) == 1) {
				asprintf (&fmt, "%s%sRSS >= %d", (fmt ? fmt : ""), (options ? ", " : ""), rss);
				options |= RSS;
				break;
			}
			usage4 (_("RSS must be an integer!"));
		case 'z':					/* VSZ */
			if (sscanf (optarg, "%d%[^0-9]", &vsz, tmp) == 1) {
				asprintf (&fmt, "%s%sVSZ >= %d", (fmt ? fmt : ""), (options ? ", " : ""), vsz);
				options |= VSZ;
				break;
			}
			usage4 (_("VSZ must be an integer!"));
		case 'P':					/* PCPU */
			/* TODO: -P 1.5.5 is accepted */
			if (sscanf (optarg, "%f%[^0-9.]", &pcpu, tmp) == 1) {
				asprintf (&fmt, "%s%sPCPU >= %.2f", (fmt ? fmt : ""), (options ? ", " : ""), pcpu);
				options |= PCPU;
				break;
			}
			usage4 (_("PCPU must be a float!"));
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
			else if ( strcmp(optarg, "ELAPSED") == 0) {
				metric = METRIC_ELAPSED;
				break;
			}
				
			usage4 (_("Metric must be one of PROCS, VSZ, RSS, CPU, ELAPSED!"));
		case 'v':									/* command */
			verbose++;
			break;
		case CHAR_MAX+2:
			input_filename = optarg;
			break;
		case CHAR_MAX+3:
			number_threshold = parse_thresholds_string(optarg);
			if (metric == DEFAULT)
				default_metric=NONE;
			break;
		case CHAR_MAX+4:
			rss_threshold = parse_thresholds_string(optarg);
			new_style_thresholds++;
			if (metric == DEFAULT)
				default_metric=NONE;
			break;
		case CHAR_MAX+7:
			vsz_threshold = parse_thresholds_string(optarg);
			new_style_thresholds++;
			if (metric == DEFAULT)
				default_metric=NONE;
			break;
		case CHAR_MAX+10:
			cpu_threshold = parse_thresholds_string(optarg);
			new_style_thresholds++;
			if (metric == DEFAULT)
				default_metric=NONE;
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

	if (metric==DEFAULT)
		metric = default_metric;

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
actions_on_failed_state(int state, char *procprog) {
	result = max_state (result, state);
	if (state != STATE_WARNING && state != STATE_CRITICAL)
		return;
	if (state == STATE_WARNING) {
		warn++;
	}
	if (state == STATE_CRITICAL) {
		crit++;
	}
	/* TODO: This should be a hash, to remove duplicates */
	asprintf (&fails, "%s%s%s", fails, (strcmp(fails,"") ? ", " : ""), procprog);
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
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	print_usage ();

  printf ("\n\n");

	printf("Checks all processes and, optionally, filters to a subset to check thresholds values against.\n");
	printf("Can specify any of the following thresholds:\n");

	printf("  --number=THRESHOLD        - Compares the number of matching processes\n");
	printf("  --vsz-threshold=THRESHOLD - Compares each process' vsz (in kilobytes)\n");
	printf("  --rss-threshold=THRESHOLD - Compares each process' rss (in kilobytes)\n");
	printf("  --cpu-threshold=THRESHOLD - Compares each process' cpu (in %%)\n");
	/* TODO: Add support for etime */
	printf("\n\n");

	printf ("%s\n", _("Optional Arguments:"));
	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (" %s\n", "-v, --verbose");
  printf ("    %s\n", _("Extra information. Up to 3 verbosity levels"));

	printf ("%s\n", "Optional Filters:");
  printf (" %s\n", "-s, --state=STATUSFLAGS");
  printf ("   %s\n", _("Only scan for processes that have, in the output of `ps`, one or"));
  printf ("   %s\n", _("more of the status flags you specify (for example R, Z, S, RS,"));
  printf ("   %s\n", _("RSZDT, plus others based on the output of your 'ps' command)."));
  printf (" %s\n", "-p, --ppid=PPID");
  printf ("   %s\n", _("Only scan for children of the parent process ID indicated."));
  printf (" %s\n", "-z, --vsz=VSZ");
  printf ("   %s\n", _("Only scan for processes with vsz higher than indicated."));
  printf (" %s\n", "-r, --rss=RSS");
  printf ("   %s\n", _("Only scan for processes with rss higher than indicated."));
	printf (" %s\n", "-P, --pcpu=PCPU");
  printf ("   %s\n", _("Only scan for processes with pcpu higher than indicated."));
  printf (" %s\n", "-u, --user=USER");
  printf ("   %s\n", _("Only scan for processes with user name or ID indicated."));
  printf (" %s\n", "-a, --argument-array=STRING");
  printf ("   %s\n", _("Only scan for processes with args that contain STRING."));
  printf (" %s\n", "--ereg-argument-array=STRING");
  printf ("   %s\n", _("Only scan for processes with args that contain the regex STRING."));
  printf (" %s\n", "-C, --command=COMMAND");
  printf ("   %s\n", _("Only scan for exact matches of COMMAND (without path)."));

	printf("\n");

	printf("\
THRESHOLDS are specified as 'critical_range/warning_range' where\n\
RANGES are defined as 'min:max'. max can be removed if it is infinity.\n\
Alerts will occur inside this range, unless you specify '^' before\n\
the range, to mean alert outside this range\n\n");

	printf ("%s\n", _("Examples:"));
  printf (" %s\n", "check_procs --number=:2/5: -C portsentry");
  printf ("  %s\n", _("Warning if greater than five processes with command name portsentry."));
  printf ("  %s\n\n", _("Critical if <= 2 processes"));
  printf (" %s\n", "check_procs --vsz-threshold=100:/50:");
  printf ("  %s\n\n", _("Warning if vsz of any processes is over 50K or critical if vsz is over 100K"));
  printf (" %s\n", "check_procs --cpu-threshold=20:/10: --ereg-argument-array='java.*server'");
  printf ("  %s\n\n", _("For all processes with arguments matching the regular expression, warning if cpu is over 10% or critical if over 20%"));
  printf (" %s\n", "check_procs --rss-threshold=100: --number=/:10 --cpu-threshold=30:/10: -a '/usr/local/bin/perl' -u root");
  printf ("  %s\n", _("Critical if rss >= 100K, or warning if total number of process <= 10, or critical if cpu >= 30% or warning if cpu >= 10%."));
  printf ("  %s\n", _("Filter by arguments containing '/usr/local/bin/perl' and owned by root"));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
  printf (_("Usage: "));
	printf ("%s -w <range> -c <range> [-m metric] [-s state] [-p ppid]\n", progname);
  printf (" [-u user] [-r rss] [-z vsz] [-P %%cpu] [-a argument-array]\n");
  printf (" [-C command] [-t timeout] [-v]\n");
}
