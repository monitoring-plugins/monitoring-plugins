/*****************************************************************************
 *
 * Monitoring check_load plugin
 *
 * License: GPL
 * Copyright (c) 1999-2007 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_load plugin
 *
 * This plugin tests the current system load average.
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

const char *progname = "check_load";
const char *copyright = "1999-2022";
const char *email = "devel@monitoring-plugins.org";

#include "./common.h"
#include <string.h>
#include "./runcmd.h"
#include "./utils.h"
#include "./popen.h"
#include "../lib/states.h"
#include "../lib/output.h"
#include "../lib/perfdata.h"
#include "../lib/thresholds.h"
#include "check_load.d/config.h"

// getloadavg comes from gnulib
#include "../gl/stdlib.h"

/* needed for compilation under NetBSD, as suggested by Andy Doran */
#ifndef LOADAVG_1MIN
#	define LOADAVG_1MIN  0
#	define LOADAVG_5MIN  1
#	define LOADAVG_15MIN 2
#endif /* !defined LOADAVG_1MIN */

typedef struct {
	int errorcode;
	check_load_config config;
} check_load_config_wrapper;
static check_load_config_wrapper process_arguments(int argc, char **argv);

void print_help(void);
void print_usage(void);
typedef struct {
	int errorcode;
	char **top_processes;
} top_processes_result;
static top_processes_result print_top_consuming_processes(unsigned long n_procs_to_show);

typedef struct {
	mp_range load[3];
} parsed_thresholds;
static parsed_thresholds get_threshold(char *arg) {
	size_t index;
	char *str = arg;
	char *tmp_pointer;
	bool valid = false;

	parsed_thresholds result = {
		.load =
			{
				mp_range_init(),
				mp_range_init(),
				mp_range_init(),
			},
	};

	size_t arg_length = strlen(arg);
	for (index = 0; index < 3; index++) {
		double tmp = strtod(str, &tmp_pointer);
		if (tmp_pointer == str) {
			break;
		}

		result.load[index] = mp_range_set_end(result.load[index], mp_create_pd_value(tmp));

		valid = true;
		str = tmp_pointer + 1;
		if (arg_length <= (size_t)(str - arg)) {
			break;
		}
	}

	/* empty argument or non-floatish, so warn about it and die */
	if (!index && !valid) {
		usage(_("Warning threshold must be float or float triplet!\n"));
	}

	if (index != 2) {
		/* one or more numbers were given, so fill array with last
		 * we got (most likely to NOT produce the least expected result) */
		for (size_t tmp_index = index; tmp_index < 3; tmp_index++) {
			result.load[tmp_index] = result.load[index];
		}
	}
	return result;
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	setlocale(LC_NUMERIC, "POSIX");

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_load_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_load_config config = tmp_config.config;

	double load_values[3] = {0, 0, 0};

	// this should be getloadavg from gnulib, should work everywhere™
	int error = getloadavg(load_values, 3);
	if (error != 3) {
		die(STATE_UNKNOWN, _("Failed to retrieve load values"));
	}

	mp_check overall = mp_check_init();
	if (config.output_format_set) {
		mp_set_format(config.output_format);
	}

	bool is_using_scaled_load_values = false;
	long numcpus;
	if (config.take_into_account_cpus && ((numcpus = GET_NUMBER_OF_CPUS()) > 0)) {
		is_using_scaled_load_values = true;

		double scaled_la[3] = {
			load_values[0] / numcpus,
			load_values[1] / numcpus,
			load_values[2] / numcpus,
		};

		mp_subcheck scaled_load_sc = mp_subcheck_init();
		scaled_load_sc = mp_set_subcheck_default_state(scaled_load_sc, STATE_OK);
		scaled_load_sc.output = "Scaled Load (divided by number of CPUs";

		mp_perfdata pd_scaled_load1 = perfdata_init();
		pd_scaled_load1.label = "scaled_load1";
		pd_scaled_load1 = mp_set_pd_value(pd_scaled_load1, scaled_la[0]);
		pd_scaled_load1 = mp_pd_set_thresholds(pd_scaled_load1, config.th_load[0]);

		mp_subcheck scaled_load_sc1 = mp_subcheck_init();
		scaled_load_sc1 = mp_set_subcheck_state(scaled_load_sc1, mp_get_pd_status(pd_scaled_load1));
		mp_add_perfdata_to_subcheck(&scaled_load_sc1, pd_scaled_load1);
		xasprintf(&scaled_load_sc1.output, "1 Minute: %s", pd_value_to_string(pd_scaled_load1.value));
		mp_add_subcheck_to_subcheck(&scaled_load_sc, scaled_load_sc1);

		mp_perfdata pd_scaled_load5 = perfdata_init();
		pd_scaled_load5.label = "scaled_load5";
		pd_scaled_load5 = mp_set_pd_value(pd_scaled_load5, scaled_la[1]);
		pd_scaled_load5 = mp_pd_set_thresholds(pd_scaled_load5, config.th_load[1]);

		mp_subcheck scaled_load_sc5 = mp_subcheck_init();
		scaled_load_sc5 = mp_set_subcheck_state(scaled_load_sc5, mp_get_pd_status(pd_scaled_load5));
		mp_add_perfdata_to_subcheck(&scaled_load_sc5, pd_scaled_load5);
		xasprintf(&scaled_load_sc5.output, "5 Minutes: %s", pd_value_to_string(pd_scaled_load5.value));
		mp_add_subcheck_to_subcheck(&scaled_load_sc, scaled_load_sc5);

		mp_perfdata pd_scaled_load15 = perfdata_init();
		pd_scaled_load15.label = "scaled_load15";
		pd_scaled_load15 = mp_set_pd_value(pd_scaled_load15, scaled_la[2]);
		pd_scaled_load15 = mp_pd_set_thresholds(pd_scaled_load15, config.th_load[2]);

		mp_subcheck scaled_load_sc15 = mp_subcheck_init();
		scaled_load_sc15 = mp_set_subcheck_state(scaled_load_sc15, mp_get_pd_status(pd_scaled_load15));
		mp_add_perfdata_to_subcheck(&scaled_load_sc15, pd_scaled_load15);
		xasprintf(&scaled_load_sc15.output, "15 Minutes: %s", pd_value_to_string(pd_scaled_load15.value));
		mp_add_subcheck_to_subcheck(&scaled_load_sc, scaled_load_sc15);

		mp_add_subcheck_to_check(&overall, scaled_load_sc);
	}

	mp_subcheck load_sc = mp_subcheck_init();
	load_sc = mp_set_subcheck_default_state(load_sc, STATE_OK);
	load_sc.output = "Total Load";

	mp_perfdata pd_load1 = perfdata_init();
	pd_load1.label = "load1";
	pd_load1 = mp_set_pd_value(pd_load1, load_values[0]);
	if (!is_using_scaled_load_values) {
		pd_load1 = mp_pd_set_thresholds(pd_load1, config.th_load[0]);
	}

	mp_subcheck load_sc1 = mp_subcheck_init();
	load_sc1 = mp_set_subcheck_state(load_sc1, mp_get_pd_status(pd_load1));
	mp_add_perfdata_to_subcheck(&load_sc1, pd_load1);
	xasprintf(&load_sc1.output, "1 Minute: %s", pd_value_to_string(pd_load1.value));
	mp_add_subcheck_to_subcheck(&load_sc, load_sc1);

	mp_perfdata pd_load5 = perfdata_init();
	pd_load5.label = "load5";
	pd_load5 = mp_set_pd_value(pd_load5, load_values[1]);
	if (!is_using_scaled_load_values) {
		pd_load5 = mp_pd_set_thresholds(pd_load5, config.th_load[1]);
	}

	mp_subcheck load_sc5 = mp_subcheck_init();
	load_sc5 = mp_set_subcheck_state(load_sc5, mp_get_pd_status(pd_load5));
	mp_add_perfdata_to_subcheck(&load_sc5, pd_load5);
	xasprintf(&load_sc5.output, "5 Minutes: %s", pd_value_to_string(pd_load5.value));
	mp_add_subcheck_to_subcheck(&load_sc, load_sc5);

	mp_perfdata pd_load15 = perfdata_init();
	pd_load15.label = "load15";
	pd_load15 = mp_set_pd_value(pd_load15, load_values[2]);
	if (!is_using_scaled_load_values) {
		pd_load15 = mp_pd_set_thresholds(pd_load15, config.th_load[2]);
	}

	mp_subcheck load_sc15 = mp_subcheck_init();
	load_sc15 = mp_set_subcheck_state(load_sc15, mp_get_pd_status(pd_load15));
	mp_add_perfdata_to_subcheck(&load_sc15, pd_load15);
	xasprintf(&load_sc15.output, "15 Minutes: %s", pd_value_to_string(pd_load15.value));
	mp_add_subcheck_to_subcheck(&load_sc, load_sc15);

	mp_add_subcheck_to_check(&overall, load_sc);

	if (config.n_procs_to_show > 0) {
		mp_subcheck top_proc_sc = mp_subcheck_init();
		top_proc_sc = mp_set_subcheck_state(top_proc_sc, STATE_OK);
		top_processes_result top_proc = print_top_consuming_processes(config.n_procs_to_show);
		xasprintf(&top_proc_sc.output, "Top %lu CPU time consuming processes", config.n_procs_to_show);

		if (top_proc.errorcode == OK) {
			for (unsigned long i = 0; i < config.n_procs_to_show; i++) {
				xasprintf(&top_proc_sc.output, "%s\n%s", top_proc_sc.output, top_proc.top_processes[i]);
			}
		}

		mp_add_subcheck_to_check(&overall, top_proc_sc);
	}

	mp_exit(overall);
}

/* process command-line arguments */
static check_load_config_wrapper process_arguments(int argc, char **argv) {

	enum {
		output_format_index = CHAR_MAX + 1,
	};

	static struct option longopts[] = {{"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"percpu", no_argument, 0, 'r'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"procs-to-show", required_argument, 0, 'n'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_load_config_wrapper result = {
		.errorcode = OK,
		.config = check_load_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	while (true) {
		int option = 0;
		int option_index = getopt_long(argc, argv, "Vhrc:w:n:", longopts, &option);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		case 'w': /* warning time threshold */ {
			parsed_thresholds warning_range = get_threshold(optarg);
			result.config.th_load[0].warning = warning_range.load[0];
			result.config.th_load[0].warning_is_set = true;

			result.config.th_load[1].warning = warning_range.load[1];
			result.config.th_load[1].warning_is_set = true;

			result.config.th_load[2].warning = warning_range.load[2];
			result.config.th_load[2].warning_is_set = true;
		} break;
		case 'c': /* critical time threshold */ {
			parsed_thresholds critical_range = get_threshold(optarg);
			result.config.th_load[0].critical = critical_range.load[0];
			result.config.th_load[0].critical_is_set = true;

			result.config.th_load[1].critical = critical_range.load[1];
			result.config.th_load[1].critical_is_set = true;

			result.config.th_load[2].critical = critical_range.load[2];
			result.config.th_load[2].critical_is_set = true;
		} break;
		case 'r': /* Divide load average by number of CPUs */
			result.config.take_into_account_cpus = true;
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'n':
			result.config.n_procs_to_show = (unsigned long)atol(optarg);
			break;
		case '?': /* help */
			usage5();
		}
	}

	int index = optind;
	if (index == argc) {
		return result;
	}

	/* handle the case if both arguments are missing,
	 * but not if only one is given without -c or -w flag */
	if (index - argc == 2) {
		parsed_thresholds warning_range = get_threshold(argv[index++]);
		result.config.th_load[0].warning = warning_range.load[0];
		result.config.th_load[0].warning_is_set = true;

		result.config.th_load[1].warning = warning_range.load[1];
		result.config.th_load[1].warning_is_set = true;

		result.config.th_load[2].warning = warning_range.load[2];
		result.config.th_load[2].warning_is_set = true;
		parsed_thresholds critical_range = get_threshold(argv[index++]);
		result.config.th_load[0].critical = critical_range.load[0];
		result.config.th_load[0].critical_is_set = true;

		result.config.th_load[1].critical = critical_range.load[1];
		result.config.th_load[1].critical_is_set = true;

		result.config.th_load[2].critical = critical_range.load[2];
		result.config.th_load[2].critical_is_set = true;
	} else if (index - argc == 1) {
		parsed_thresholds critical_range = get_threshold(argv[index++]);
		result.config.th_load[0].critical = critical_range.load[0];
		result.config.th_load[0].critical_is_set = true;

		result.config.th_load[1].critical = critical_range.load[1];
		result.config.th_load[1].critical_is_set = true;

		result.config.th_load[2].critical = critical_range.load[2];
		result.config.th_load[2].critical_is_set = true;
	}

	return result;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin tests the current system load average."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-w, --warning=WLOAD1,WLOAD5,WLOAD15");
	printf("    %s\n", _("Exit with WARNING status if load average exceeds WLOADn"));
	printf(" %s\n", "-c, --critical=CLOAD1,CLOAD5,CLOAD15");
	printf("    %s\n", _("Exit with CRITICAL status if load average exceed CLOADn"));
	printf("    %s\n", _("the load average format is the same used by \"uptime\" and \"w\""));
	printf(" %s\n", "-r, --percpu");
	printf("    %s\n", _("Divide the load averages by the number of CPUs (when possible)"));
	printf(" %s\n", "-n, --procs-to-show=NUMBER_OF_PROCS");
	printf("    %s\n", _("Number of processes to show when printing the top consuming processes."));
	printf("    %s\n", _("NUMBER_OF_PROCS=0 disables this feature. Default value is 0"));

	printf(UT_OUTPUT_FORMAT);
	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s [-r] -w WLOAD1,WLOAD5,WLOAD15 -c CLOAD1,CLOAD5,CLOAD15 [-n NUMBER_OF_PROCS]\n", progname);
}

#ifdef PS_USES_PROCPCPU
int cmpstringp(const void *p1, const void *p2) {
	int procuid = 0;
	int procpid = 0;
	int procppid = 0;
	int procvsz = 0;
	int procrss = 0;
	float procpcpu = 0;
	char procstat[8];
#	ifdef PS_USES_PROCETIME
	char procetime[MAX_INPUT_BUFFER];
#	endif /* PS_USES_PROCETIME */
	char procprog[MAX_INPUT_BUFFER];
	int pos;
	sscanf(*(char *const *)p1, PS_FORMAT, PS_VARLIST);
	float procpcpu1 = procpcpu;
	sscanf(*(char *const *)p2, PS_FORMAT, PS_VARLIST);
	return procpcpu1 < procpcpu;
}
#endif /* PS_USES_PROCPCPU */

static top_processes_result print_top_consuming_processes(unsigned long n_procs_to_show) {
	top_processes_result result = {
		.errorcode = OK,
	};
	struct output chld_out;
	struct output chld_err;
	if (np_runcmd(PS_COMMAND, &chld_out, &chld_err, 0) != 0) {
		fprintf(stderr, _("'%s' exited with non-zero status.\n"), PS_COMMAND);
		result.errorcode = ERROR;
		return result;
	}

	if (chld_out.lines < 2) {
		fprintf(stderr, _("some error occurred getting procs list.\n"));
		result.errorcode = ERROR;
		return result;
	}

#ifdef PS_USES_PROCPCPU
	qsort(chld_out.line + 1, chld_out.lines - 1, sizeof(char *), cmpstringp);
#endif /* PS_USES_PROCPCPU */
	unsigned long lines_to_show = chld_out.lines < (size_t)(n_procs_to_show + 1) ? chld_out.lines : n_procs_to_show + 1;

	result.top_processes = calloc(lines_to_show, sizeof(char *));
	if (result.top_processes == NULL) {
		// Failed allocation
		result.errorcode = ERROR;
		return result;
	}

	for (unsigned long i = 0; i < lines_to_show; i += 1) {
		xasprintf(&result.top_processes[i], "%s", chld_out.line[i]);
	}

	return result;
}
