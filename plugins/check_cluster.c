/*****************************************************************************
 *
 * check_cluster.c - Host and Service Cluster Plugin for Monitoring
 *
 * License: GPL
 * Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2007-2024 Monitoring Plugins Development Team
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

const char *progname = "check_cluster";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "output.h"
#include "states.h"
#include "common.h"
#include "utils.h"
#include "utils_base.h"
#include "check_cluster.d/config.h"

static void print_help(void);
void print_usage(void);

static int verbose = 0;

typedef struct {
	int errorcode;
	check_cluster_config config;
} check_cluster_config_wrapper;
static check_cluster_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_cluster_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage(_("Could not parse arguments"));
	}

	const check_cluster_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	/* Initialize the thresholds */
	if (verbose) {
		print_thresholds("check_cluster", config.thresholds);
	}

	int data_val;
	int total_services_ok = 0;
	int total_services_warning = 0;
	int total_services_unknown = 0;
	int total_services_critical = 0;
	int total_hosts_up = 0;
	int total_hosts_down = 0;
	int total_hosts_unreachable = 0;
	/* check the data values */
	for (char *ptr = strtok(config.data_vals, ","); ptr != NULL; ptr = strtok(NULL, ",")) {
		data_val = atoi(ptr);

		if (config.check_type == CHECK_SERVICES) {
			switch (data_val) {
			case 0:
				total_services_ok++;
				break;
			case 1:
				total_services_warning++;
				break;
			case 2:
				total_services_critical++;
				break;
			case 3:
				total_services_unknown++;
				break;
			default:
				break;
			}
		} else {
			switch (data_val) {
			case 0:
				total_hosts_up++;
				break;
			case 1:
				total_hosts_down++;
				break;
			case 2:
				total_hosts_unreachable++;
				break;
			default:
				break;
			}
		}
	}

	mp_check overall = mp_check_init();
	mp_subcheck sc_real_test = mp_subcheck_init();
	sc_real_test = mp_set_subcheck_default_state(sc_real_test, STATE_OK);

	/* return the status of the cluster */
	if (config.check_type == CHECK_SERVICES) {
		sc_real_test = mp_set_subcheck_state(
			sc_real_test,
			get_status(total_services_warning + total_services_unknown + total_services_critical,
					   config.thresholds));
		xasprintf(&sc_real_test.output, "%s: %d ok, %d warning, %d unknown, %d critical",
				  (config.label == NULL) ? "Service cluster" : config.label, total_services_ok,
				  total_services_warning, total_services_unknown, total_services_critical);
	} else {
		sc_real_test = mp_set_subcheck_state(
			sc_real_test,
			get_status(total_hosts_down + total_hosts_unreachable, config.thresholds));
		xasprintf(&sc_real_test.output, "%s: %d up, %d down, %d unreachable\n",
				  (config.label == NULL) ? "Host cluster" : config.label, total_hosts_up,
				  total_hosts_down, total_hosts_unreachable);
	}

	mp_add_subcheck_to_check(&overall, sc_real_test);

	mp_exit(overall);
}

check_cluster_config_wrapper process_arguments(int argc, char **argv) {
	enum {
		output_format_index = CHAR_MAX + 1,
	};

	static struct option longopts[] = {{"data", required_argument, 0, 'd'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"label", required_argument, 0, 'l'},
									   {"host", no_argument, 0, 'h'},
									   {"service", no_argument, 0, 's'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'H'},
									   {"output-format", required_argument, 0, output_format_index},
									   {0, 0, 0, 0}};

	check_cluster_config_wrapper result = {
		.errorcode = OK,
		.config = check_cluster_config_init(),
	};

	/* no options were supplied */
	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	int option = 0;
	char *warn_threshold = NULL;
	char *crit_threshold = NULL;
	while (true) {
		int option_index = getopt_long(argc, argv, "hHsvVw:c:d:l:", longopts, &option);

		if (CHECK_EOF(option_index) || option_index == 1) {
			break;
		}

		switch (option_index) {
		case 'h': /* host cluster */
			result.config.check_type = CHECK_HOSTS;
			break;
		case 's': /* service cluster */
			result.config.check_type = CHECK_SERVICES;
			break;
		case 'w': /* warning threshold */
			warn_threshold = strdup(optarg);
			break;
		case 'c': /* warning threshold */
			crit_threshold = strdup(optarg);
			break;
		case 'd': /* data values */
			result.config.data_vals = strdup(optarg);
			/* validate data */
			for (char *ptr = result.config.data_vals; ptr != NULL; ptr += 2) {
				if (ptr[0] < '0' || ptr[0] > '3') {
					result.errorcode = ERROR;
					return result;
				}
				if (ptr[1] == '\0') {
					break;
				}
				if (ptr[1] != ',') {
					result.errorcode = ERROR;
					return result;
				}
			}
			break;
		case 'l': /* text label */
			result.config.label = strdup(optarg);
			break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
			break;
		case 'H': /* help */
			print_help();
			exit(STATE_UNKNOWN);
			break;
		case output_format_index: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		default:
			result.errorcode = ERROR;
			return result;
			break;
		}
	}

	if (result.config.data_vals == NULL) {
		result.errorcode = ERROR;
		return result;
	}

	set_thresholds(&result.config.thresholds, warn_threshold, crit_threshold);
	return result;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);
	printf("Copyright (c) 2000-2004 Ethan Galstad (nagios@nagios.org)\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("Host/Service Cluster Plugin for Monitoring"));
	printf("\n\n");

	print_usage();

	printf("\n");
	printf("%s\n", _("Options:"));
	printf(UT_EXTRA_OPTS);
	printf(" %s\n", "-s, --service");
	printf("    %s\n", _("Check service cluster status"));
	printf(" %s\n", "-h, --host");
	printf("    %s\n", _("Check host cluster status"));
	printf(" %s\n", "-l, --label=STRING");
	printf("    %s\n", _("Optional prepended text output (i.e. \"Host cluster\")"));
	printf(" %s\n", "-w, --warning=THRESHOLD");
	printf("    %s\n", _("Specifies the range of hosts or services in cluster that must be in a"));
	printf("    %s\n", _("non-OK state in order to return a WARNING status level"));
	printf(" %s\n", "-c, --critical=THRESHOLD");
	printf("    %s\n", _("Specifies the range of hosts or services in cluster that must be in a"));
	printf("    %s\n", _("non-OK state in order to return a CRITICAL status level"));
	printf(" %s\n", "-d, --data=LIST");
	printf("    %s\n", _("The status codes of the hosts or services in the cluster, separated by"));
	printf("    %s\n", _("commas"));

	printf(UT_VERBOSE);

	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(UT_THRESHOLDS_NOTES);

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_cluster -s -d 2,0,2,0 -c @3:");
	printf("    %s\n",
		   _("Will alert critical if there are 3 or more service data points in a non-OK"));
	printf("    %s\n", _("state."));

	printf(UT_SUPPORT);
}

void print_usage(void) {

	printf("%s\n", _("Usage:"));
	printf(" %s (-s | -h) -d val1[,val2,...,valn] [-l label]\n", progname);
	printf("[-w threshold] [-c threshold] [-v] [--help]\n");
}
