/*****************************************************************************
 *
 * Monitoring check_procs plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
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
 * The parent process, check_procs itself and any child process of
 * check_procs (ps) are excluded from any checks to prevent false positives.
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
const char *program_name = "check_procs"; /* Required for coreutils libs */
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "utils_cmd.h"
#include "regex.h"
#include "states.h"
#include "check_procs.d/config.h"

#include <pwd.h>
#include <errno.h>

#ifdef HAVE_SYS_STAT_H
#	include <sys/stat.h>
#endif

typedef struct {
	int errorcode;
	check_procs_config config;
} check_procs_config_wrapper;
static check_procs_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_procs_config_wrapper validate_arguments(check_procs_config_wrapper /*config_wrapper*/);

static int convert_to_seconds(char * /*etime*/, enum metric /*metric*/);
static void print_help(void);
void print_usage(void);

#define ALL           1
#define STAT          2
#define PPID          4
#define USER          8
#define PROG          16
#define ARGS          32
#define VSZ           64
#define RSS           128
#define PCPU          256
#define ELAPSED       512
#define EREG_ARGS     1024
#define EXCLUDE_PROGS 2048

#define KTHREAD_PARENT                                                                             \
	"kthreadd" /* the parent process of kernel threads:                                            \
		 ppid of procs are compared to pid of this proc*/

static int verbose = 0;

static int stat_exe(const pid_t pid, struct stat *buf) {
	char *path;
	xasprintf(&path, "/proc/%d/exe", pid);
	int ret = stat(path, buf);
	free(path);
	return ret;
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	setlocale(LC_NUMERIC, "POSIX");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_procs_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_procs_config config = tmp_config.config;

	/* find ourself */
	pid_t mypid = getpid();
	pid_t myppid = getppid();
	dev_t mydev = 0;
	ino_t myino = 0;
	struct stat statbuf;
	if (config.usepid || stat_exe(mypid, &statbuf) == -1) {
		/* usepid might have been set by -T */
		config.usepid = true;
	} else {
		config.usepid = false;
		mydev = statbuf.st_dev;
		myino = statbuf.st_ino;
	}

	/* Set signal handling and alarm timeout */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		die(STATE_UNKNOWN, _("Cannot catch SIGALRM"));
	}
	(void)alarm(timeout_interval);

	if (verbose >= 2) {
		printf(_("CMD: %s\n"), PS_COMMAND);
	}

	output chld_out;
	output chld_err;
	mp_state_enum result = STATE_UNKNOWN;
	if (config.input_filename == NULL) {
		result = cmd_run(PS_COMMAND, &chld_out, &chld_err, 0);
		if (chld_err.lines > 0) {
			printf("%s: %s", _("System call sent warnings to stderr"), chld_err.line[0]);
			exit(STATE_WARNING);
		}
	} else {
		result = cmd_file_read(config.input_filename, &chld_out, 0);
	}

	int pos; /* number of spaces before 'args' in `ps` output */
	uid_t procuid = 0;
	pid_t procpid = 0;
	pid_t procppid = 0;
	pid_t kthread_ppid = 0;
	int warn = 0; /* number of processes in warn state */
	int crit = 0; /* number of processes in crit state */
	int procvsz = 0;
	int procrss = 0;
	int procseconds = 0;
	float procpcpu = 0;
	char procstat[8];
	char procetime[MAX_INPUT_BUFFER] = {'\0'};
	int resultsum = 0; /* bitmask of the filter criteria met by a process */
	int found = 0;     /* counter for number of lines returned in `ps` output */
	int procs = 0;     /* counter for number of processes meeting filter criteria */
	char *input_buffer = malloc(MAX_INPUT_BUFFER);
	char *procprog = malloc(MAX_INPUT_BUFFER);
	const int expected_cols = PS_COLS - 1;

	/* flush first line: j starts at 1 */
	for (size_t j = 1; j < chld_out.lines; j++) {
		char *input_line = chld_out.line[j];

		if (verbose >= 3) {
			printf("%s", input_line);
		}

		strcpy(procprog, "");
		char *procargs;
		xasprintf(&procargs, "%s", "");

		/* number of columns in ps output */
		int cols = sscanf(input_line, PS_FORMAT, PS_VARLIST);

		/* Zombie processes do not give a procprog command */
		const char *zombie = "Z";
		if (cols < expected_cols && strstr(procstat, zombie)) {
			cols = expected_cols;
		}
		if (cols >= expected_cols) {
			resultsum = 0;
			xasprintf(&procargs, "%s", input_line + pos);
			strip(procargs);

			/* Some ps return full pathname for command. This removes path */
			strcpy(procprog, base_name(procprog));

			/* we need to convert the elapsed time to seconds */
			procseconds = convert_to_seconds(procetime, config.metric);

			if (verbose >= 3) {
				printf("proc#=%d uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s "
					   "prog=%s args=%s\n",
					   procs, procuid, procvsz, procrss, procpid, procppid, procpcpu, procstat,
					   procetime, procprog, procargs);
			}

			/* Ignore self */
			int ret = 0;
			if ((config.usepid && mypid == procpid) ||
				(((!config.usepid) && ((ret = stat_exe(procpid, &statbuf) != -1) &&
									   statbuf.st_dev == mydev && statbuf.st_ino == myino)) ||
				 (ret == -1 && errno == ENOENT))) {
				if (verbose >= 3) {
					printf("not considering - is myself or gone\n");
				}
				continue;
			}
			/* Ignore parent*/
			if (myppid == procpid) {
				if (verbose >= 3) {
					printf("not considering - is parent\n");
				}
				continue;
			}

			/* Ignore our own children */
			if (procppid == mypid) {
				if (verbose >= 3) {
					printf("not considering - is our child\n");
				}
				continue;
			}

			/* Ignore excluded processes by name */
			if (config.options & EXCLUDE_PROGS) {
				bool found = false;
				for (int i = 0; i < (config.exclude_progs_counter); i++) {
					if (!strcmp(procprog, config.exclude_progs_arr[i])) {
						found = true;
					}
				}
				if (!found) {
					resultsum |= EXCLUDE_PROGS;
				} else {
					if (verbose >= 3) {
						printf("excluding - by ignorelist\n");
					}
				}
			}

			/* filter kernel threads (children of KTHREAD_PARENT)*/
			/* TODO adapt for other OSes than GNU/Linux
					sorry for not doing that, but I've no other OSes to test :-( */
			if (config.kthread_filter) {
				/* get pid KTHREAD_PARENT */
				if (kthread_ppid == 0 && !strcmp(procprog, KTHREAD_PARENT)) {
					kthread_ppid = procpid;
				}

				if (kthread_ppid == procppid) {
					if (verbose >= 2) {
						printf("Ignore kernel thread: pid=%d ppid=%d prog=%s args=%s\n", procpid,
							   procppid, procprog, procargs);
					}
					continue;
				}
			}

			if ((config.options & STAT) && (strstr(procstat, config.statopts))) {
				resultsum |= STAT;
			}
			if ((config.options & ARGS) && procargs && (strstr(procargs, config.args) != NULL)) {
				resultsum |= ARGS;
			}
			if ((config.options & EREG_ARGS) && procargs &&
				(regexec(&config.re_args, procargs, (size_t)0, NULL, 0) == 0)) {
				resultsum |= EREG_ARGS;
			}
			if ((config.options & PROG) && procprog && (strcmp(config.prog, procprog) == 0)) {
				resultsum |= PROG;
			}
			if ((config.options & PPID) && (procppid == config.ppid)) {
				resultsum |= PPID;
			}
			if ((config.options & USER) && (procuid == config.uid)) {
				resultsum |= USER;
			}
			if ((config.options & VSZ) && (procvsz >= config.vsz)) {
				resultsum |= VSZ;
			}
			if ((config.options & RSS) && (procrss >= config.rss)) {
				resultsum |= RSS;
			}
			if ((config.options & PCPU) && (procpcpu >= config.pcpu)) {
				resultsum |= PCPU;
			}

			found++;

			/* Next line if filters not matched */
			if (!(config.options == resultsum || config.options == ALL)) {
				continue;
			}

			procs++;
			if (verbose >= 2) {
				printf("Matched: uid=%d vsz=%d rss=%d pid=%d ppid=%d pcpu=%.2f stat=%s etime=%s "
					   "prog=%s args=%s\n",
					   procuid, procvsz, procrss, procpid, procppid, procpcpu, procstat, procetime,
					   procprog, procargs);
			}

			mp_state_enum temporary_result = STATE_OK;
			if (config.metric == METRIC_VSZ) {
				temporary_result = get_status((double)procvsz, config.procs_thresholds);
			} else if (config.metric == METRIC_RSS) {
				temporary_result = get_status((double)procrss, config.procs_thresholds);
			}
			/* TODO? float thresholds for --metric=CPU */
			else if (config.metric == METRIC_CPU) {
				temporary_result = get_status(procpcpu, config.procs_thresholds);
			} else if (config.metric == METRIC_ELAPSED) {
				temporary_result = get_status((double)procseconds, config.procs_thresholds);
			}

			if (config.metric != METRIC_PROCS) {
				if (temporary_result == STATE_WARNING) {
					warn++;
					xasprintf(&config.fails, "%s%s%s", config.fails,
							  (strcmp(config.fails, "") ? ", " : ""), procprog);
					result = max_state(result, temporary_result);
				}
				if (temporary_result == STATE_CRITICAL) {
					crit++;
					xasprintf(&config.fails, "%s%s%s", config.fails,
							  (strcmp(config.fails, "") ? ", " : ""), procprog);
					result = max_state(result, temporary_result);
				}
			}
		}
		/* This should not happen */
		else if (verbose) {
			printf(_("Not parseable: %s"), input_buffer);
		}
	}

	if (found == 0) { /* no process lines parsed so return STATE_UNKNOWN */
		printf(_("Unable to read output\n"));
		return STATE_UNKNOWN;
	}

	if (result == STATE_UNKNOWN) {
		result = STATE_OK;
	}

	/* Needed if procs found, but none match filter */
	if (config.metric == METRIC_PROCS) {
		result = max_state(result, get_status((double)procs, config.procs_thresholds));
	}

	if (result == STATE_OK) {
		printf("%s %s: ", config.metric_name, _("OK"));
	} else if (result == STATE_WARNING) {
		printf("%s %s: ", config.metric_name, _("WARNING"));
		if (config.metric != METRIC_PROCS) {
			printf(_("%d warn out of "), warn);
		}
	} else if (result == STATE_CRITICAL) {
		printf("%s %s: ", config.metric_name, _("CRITICAL"));
		if (config.metric != METRIC_PROCS) {
			printf(_("%d crit, %d warn out of "), crit, warn);
		}
	}
	printf(ngettext("%d process", "%d processes", (unsigned long)procs), procs);

	if (strcmp(config.fmt, "") != 0) {
		printf(_(" with %s"), config.fmt);
	}

	if (verbose >= 1 && strcmp(config.fails, "")) {
		printf(" [%s]", config.fails);
	}

	if (config.metric == METRIC_PROCS) {
		printf(" | procs=%d;%s;%s;0;", procs, config.warning_range ? config.warning_range : "",
			   config.critical_range ? config.critical_range : "");
	} else {
		printf(" | procs=%d;;;0; procs_warn=%d;;;0; procs_crit=%d;;;0;", procs, warn, crit);
	}

	printf("\n");
	exit(result);
}

/* process command-line arguments */
check_procs_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"warning", required_argument, 0, 'w'},
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
									   {"ereg-argument-array", required_argument, 0, CHAR_MAX + 1},
									   {"input-file", required_argument, 0, CHAR_MAX + 2},
									   {"no-kthreads", required_argument, 0, 'k'},
									   {"traditional-filter", no_argument, 0, 'T'},
									   {"exclude-process", required_argument, 0, 'X'},
									   {0, 0, 0, 0}};

	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
	}

	check_procs_config_wrapper result = {
		.errorcode = OK,
		.config = check_procs_config_init(),
	};

	while (true) {
		int option = 0;
		int option_index =
			getopt_long(argc, argv, "Vvhkt:c:w:p:s:u:C:a:z:r:m:P:TX:", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case '?': /* help */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 't': /* timeout period */
			if (!is_integer(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				timeout_interval = atoi(optarg);
			}
			break;
		case 'c': /* critical threshold */
			result.config.critical_range = optarg;
			break;
		case 'w': /* warning threshold */
			result.config.warning_range = optarg;
			break;
		case 'p': { /* process id */
			static char tmp[MAX_INPUT_BUFFER];
			if (sscanf(optarg, "%d%[^0-9]", &result.config.ppid, tmp) == 1) {
				xasprintf(&result.config.fmt, "%s%sPPID = %d",
						  (result.config.fmt ? result.config.fmt : ""),
						  (result.config.options ? ", " : ""), result.config.ppid);
				result.config.options |= PPID;
				break;
			}
			usage4(_("Parent Process ID must be an integer!"));
		}
		case 's': /* status */
			if (result.config.statopts) {
				break;
			} else {
				result.config.statopts = optarg;
			}
			xasprintf(&result.config.fmt, _("%s%sSTATE = %s"),
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), result.config.statopts);
			result.config.options |= STAT;
			break;
		case 'u': /* user or user id */ {
			struct passwd *pw;
			if (is_integer(optarg)) {
				result.config.uid = atoi(optarg);
				pw = getpwuid(result.config.uid);
				/*  check to be sure user exists */
				if (pw == NULL) {
					usage2(_("UID was not found"), optarg);
				}
			} else {
				pw = getpwnam(optarg);
				/*  check to be sure user exists */
				if (pw == NULL) {
					usage2(_("User name was not found"), optarg);
				}
				/*  then get uid */
				result.config.uid = pw->pw_uid;
			}

			char *user = pw->pw_name;
			xasprintf(&result.config.fmt, "%s%sUID = %d (%s)",
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), result.config.uid, user);
			result.config.options |= USER;
		} break;
		case 'C': /* command */
			/* TODO: allow this to be passed in with --metric */
			if (result.config.prog) {
				break;
			} else {
				result.config.prog = optarg;
			}
			xasprintf(&result.config.fmt, _("%s%scommand name '%s'"),
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), result.config.prog);
			result.config.options |= PROG;
			break;
		case 'X':
			if (result.config.exclude_progs) {
				break;
			} else {
				result.config.exclude_progs = optarg;
			}
			xasprintf(&result.config.fmt, _("%s%sexclude progs '%s'"),
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), result.config.exclude_progs);
			char *tmp_pointer = strtok(result.config.exclude_progs, ",");

			while (tmp_pointer) {
				result.config.exclude_progs_arr =
					realloc(result.config.exclude_progs_arr,
							sizeof(char *) * ++result.config.exclude_progs_counter);
				result.config.exclude_progs_arr[result.config.exclude_progs_counter - 1] =
					tmp_pointer;
				tmp_pointer = strtok(NULL, ",");
			}

			result.config.options |= EXCLUDE_PROGS;
			break;
		case 'a': /* args (full path name with args) */
			/* TODO: allow this to be passed in with --metric */
			if (result.config.args) {
				break;
			} else {
				result.config.args = optarg;
			}
			xasprintf(&result.config.fmt, "%s%sargs '%s'",
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), result.config.args);
			result.config.options |= ARGS;
			break;
		case CHAR_MAX + 1: {
			int cflags = REG_NOSUB | REG_EXTENDED;
			int err = regcomp(&result.config.re_args, optarg, cflags);
			if (err != 0) {
				char errbuf[MAX_INPUT_BUFFER];
				regerror(err, &result.config.re_args, errbuf, MAX_INPUT_BUFFER);
				die(STATE_UNKNOWN, "PROCS %s: %s - %s\n", _("UNKNOWN"),
					_("Could not compile regular expression"), errbuf);
			}
			/* Strip off any | within the regex optarg */
			char *temp_string = strdup(optarg);
			int index = 0;
			while (temp_string[index] != '\0') {
				if (temp_string[index] == '|') {
					temp_string[index] = ',';
				}
				index++;
			}
			xasprintf(&result.config.fmt, "%s%sregex args '%s'",
					  (result.config.fmt ? result.config.fmt : ""),
					  (result.config.options ? ", " : ""), temp_string);
			result.config.options |= EREG_ARGS;
		} break;
		case 'r': { /* RSS */
			static char tmp[MAX_INPUT_BUFFER];
			if (sscanf(optarg, "%d%[^0-9]", &result.config.rss, tmp) == 1) {
				xasprintf(&result.config.fmt, "%s%sRSS >= %d",
						  (result.config.fmt ? result.config.fmt : ""),
						  (result.config.options ? ", " : ""), result.config.rss);
				result.config.options |= RSS;
				break;
			}
			usage4(_("RSS must be an integer!"));
		}
		case 'z': { /* VSZ */
			static char tmp[MAX_INPUT_BUFFER];
			if (sscanf(optarg, "%d%[^0-9]", &result.config.vsz, tmp) == 1) {
				xasprintf(&result.config.fmt, "%s%sVSZ >= %d",
						  (result.config.fmt ? result.config.fmt : ""),
						  (result.config.options ? ", " : ""), result.config.vsz);
				result.config.options |= VSZ;
				break;
			}
			usage4(_("VSZ must be an integer!"));
		}
		case 'P': { /* PCPU */
			/* TODO: -P 1.5.5 is accepted */
			static char tmp[MAX_INPUT_BUFFER];
			if (sscanf(optarg, "%f%[^0-9.]", &result.config.pcpu, tmp) == 1) {
				xasprintf(&result.config.fmt, "%s%sPCPU >= %.2f",
						  (result.config.fmt ? result.config.fmt : ""),
						  (result.config.options ? ", " : ""), result.config.pcpu);
				result.config.options |= PCPU;
				break;
			}
			usage4(_("PCPU must be a float!"));
		}
		case 'm':
			xasprintf(&result.config.metric_name, "%s", optarg);
			if (strcmp(optarg, "PROCS") == 0) {
				result.config.metric = METRIC_PROCS;
				break;
			}
			if (strcmp(optarg, "VSZ") == 0) {
				result.config.metric = METRIC_VSZ;
				break;
			}
			if (strcmp(optarg, "RSS") == 0) {
				result.config.metric = METRIC_RSS;
				break;
			}
			if (strcmp(optarg, "CPU") == 0) {
				result.config.metric = METRIC_CPU;
				break;
			}
			if (strcmp(optarg, "ELAPSED") == 0) {
				result.config.metric = METRIC_ELAPSED;
				break;
			}

			usage4(_("Metric must be one of PROCS, VSZ, RSS, CPU, ELAPSED!"));
		case 'k': /* linux kernel thread filter */
			result.config.kthread_filter = true;
			break;
		case 'v': /* command */
			verbose++;
			break;
		case 'T':
			result.config.usepid = true;
			break;
		case CHAR_MAX + 2:
			result.config.input_filename = optarg;
			break;
		}
	}

	int index = optind;
	if ((!result.config.warning_range) && argv[index]) {
		result.config.warning_range = argv[index++];
	}
	if ((!result.config.critical_range) && argv[index]) {
		result.config.critical_range = argv[index++];
	}
	if (result.config.statopts == NULL && argv[index]) {
		xasprintf(&result.config.statopts, "%s", argv[index++]);
		xasprintf(&result.config.fmt, _("%s%sSTATE = %s"),
				  (result.config.fmt ? result.config.fmt : ""), (result.config.options ? ", " : ""),
				  result.config.statopts);
		result.config.options |= STAT;
	}

	/* this will abort in case of invalid ranges */
	set_thresholds(&result.config.procs_thresholds, result.config.warning_range,
				   result.config.critical_range);

	return validate_arguments(result);
}

check_procs_config_wrapper validate_arguments(check_procs_config_wrapper config_wrapper) {
	if (config_wrapper.config.options == 0) {
		config_wrapper.config.options = ALL;
	}

	if (config_wrapper.config.statopts == NULL) {
		config_wrapper.config.statopts = strdup("");
	}

	if (config_wrapper.config.prog == NULL) {
		config_wrapper.config.prog = strdup("");
	}

	if (config_wrapper.config.args == NULL) {
		config_wrapper.config.args = strdup("");
	}

	if (config_wrapper.config.fmt == NULL) {
		config_wrapper.config.fmt = strdup("");
	}

	if (config_wrapper.config.fails == NULL) {
		config_wrapper.config.fails = strdup("");
	}

	// return options;
	return config_wrapper;
}

/* convert the elapsed time to seconds */
int convert_to_seconds(char *etime, enum metric metric) {
	int hyphcnt = 0;
	int coloncnt = 0;
	for (char *ptr = etime; *ptr != '\0'; ptr++) {

		if (*ptr == '-') {
			hyphcnt++;
			continue;
		}
		if (*ptr == ':') {
			coloncnt++;
			continue;
		}
	}

	int days = 0;
	int hours = 0;
	int minutes = 0;
	int seconds = 0;
	if (hyphcnt > 0) {
		sscanf(etime, "%d-%d:%d:%d", &days, &hours, &minutes, &seconds);
		/* linux 2.6.5/2.6.6 reporting some processes with infinite
		 * elapsed times for some reason */
		if (days == 49710) {
			return 0;
		}
	} else {
		if (coloncnt == 2) {
			sscanf(etime, "%d:%d:%d", &hours, &minutes, &seconds);
		} else if (coloncnt == 1) {
			sscanf(etime, "%d:%d", &minutes, &seconds);
		}
	}

	int total = (days * 86400) + (hours * 3600) + (minutes * 60) + seconds;

	if (verbose >= 3 && metric == METRIC_ELAPSED) {
		printf("seconds: %d\n", total);
	}
	return total;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n",
		   _("Checks all processes and generates WARNING or CRITICAL states if the specified"));
	printf("%s\n",
		   _("metric is outside the required threshold ranges. The metric defaults to number"));
	printf("%s\n",
		   _("of processes.  Search filters can be applied to limit the processes to check."));

	printf("\n\n");

	printf("%s\n",
		   _("The parent process, check_procs itself and any child process of check_procs (ps)"));
	printf("%s\n", _("are excluded from any checks to prevent false positives."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);
	printf(" %s\n", "-w, --warning=RANGE");
	printf("   %s\n", _("Generate warning state if metric is outside this range"));
	printf(" %s\n", "-c, --critical=RANGE");
	printf("   %s\n", _("Generate critical state if metric is outside this range"));
	printf(" %s\n", "-m, --metric=TYPE");
	printf("  %s\n", _("Check thresholds against metric. Valid types:"));
	printf("  %s\n", _("PROCS   - number of processes (default)"));
	printf("  %s\n", _("VSZ     - virtual memory size"));
	printf("  %s\n", _("RSS     - resident set memory size"));
	printf("  %s\n", _("CPU     - percentage CPU"));
/* only linux etime is support currently */
#if defined(__linux__)
	printf("  %s\n", _("ELAPSED - time elapsed in seconds"));
#endif /* defined(__linux__) */
	printf(UT_PLUG_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(" %s\n", "-v, --verbose");
	printf("    %s\n", _("Extra information. Up to 3 verbosity levels"));

	printf(" %s\n", "-T, --traditional");
	printf("   %s\n", _("Filter own process the traditional way by PID instead of /proc/pid/exe"));

	printf("\n");
	printf("%s\n", "Filters:");
	printf(" %s\n", "-s, --state=STATUSFLAGS");
	printf("   %s\n", _("Only scan for processes that have, in the output of `ps`, one or"));
	printf("   %s\n", _("more of the status flags you specify (for example R, Z, S, RS,"));
	printf("   %s\n", _("RSZDT, plus others based on the output of your 'ps' command)."));
	printf(" %s\n", "-p, --ppid=PPID");
	printf("   %s\n", _("Only scan for children of the parent process ID indicated."));
	printf(" %s\n", "-z, --vsz=VSZ");
	printf("   %s\n", _("Only scan for processes with VSZ higher than indicated."));
	printf(" %s\n", "-r, --rss=RSS");
	printf("   %s\n", _("Only scan for processes with RSS higher than indicated."));
	printf(" %s\n", "-P, --pcpu=PCPU");
	printf("   %s\n", _("Only scan for processes with PCPU higher than indicated."));
	printf(" %s\n", "-u, --user=USER");
	printf("   %s\n", _("Only scan for processes with user name or ID indicated."));
	printf(" %s\n", "-a, --argument-array=STRING");
	printf("   %s\n", _("Only scan for processes with args that contain STRING."));
	printf(" %s\n", "--ereg-argument-array=STRING");
	printf("   %s\n", _("Only scan for processes with args that contain the regex STRING."));
	printf(" %s\n", "-C, --command=COMMAND");
	printf("   %s\n", _("Only scan for exact matches of COMMAND (without path)."));
	printf(" %s\n", "-X, --exclude-process");
	printf("   %s\n", _("Exclude processes which match this comma separated list"));
	printf(" %s\n", "-k, --no-kthreads");
	printf("   %s\n", _("Only scan for non kernel threads (works on Linux only)."));

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

	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_procs -w 2:2 -c 2:1024 -C portsentry");
	printf("  %s\n", _("Warning if not two processes with command name portsentry."));
	printf("  %s\n\n", _("Critical if < 2 or > 1024 processes"));
	printf(" %s\n", "check_procs -c 1: -C sshd");
	printf("  %s\n", _("Critical if not at least 1 process with command sshd"));
	printf(" %s\n", "check_procs -w 1024 -c 1: -C sshd");
	printf("  %s\n", _("Warning if > 1024 processes with command name sshd."));
	printf("  %s\n\n", _("Critical if < 1 processes with command name sshd."));
	printf(" %s\n", "check_procs -w 10 -a '/usr/local/bin/perl' -u root");
	printf("  %s\n", _("Warning alert if > 10 processes with command arguments containing"));
	printf("  %s\n\n", _("'/usr/local/bin/perl' and owned by root"));
	printf(" %s\n", "check_procs -w 50000 -c 100000 --metric=VSZ");
	printf("  %s\n\n", _("Alert if VSZ of any processes over 50K or 100K"));
	printf(" %s\n", "check_procs -w 10 -c 20 --metric=CPU");
	printf("  %s\n", _("Alert if CPU of any processes over 10%% or 20%%"));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -w <range> -c <range> [-m metric] [-s state] [-p ppid]\n", progname);
	printf(" [-u user] [-r rss] [-z vsz] [-P %%cpu] [-a argument-array]\n");
	printf(" [-C command] [-X process_to_exclude] [-k] [-t timeout] [-v]\n");
}
