/*****************************************************************************
 *
 * Monitoring check_swap plugin
 *
 * License: GPL
 * Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
 * Copyright (c) 2000-2023 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_swap plugin
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

const char *progname = "check_swap";
const char *copyright = "2000-2023";
const char *email = "devel@monitoring-plugins.org";


#ifdef HAVE_DECL_SWAPCTL
#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef HAVE_SYS_SWAP_H
#include <sys/swap.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#endif

#include "./check_swap.d/check_swap.h"
#include "./utils.h"

typedef struct {
	int errorcode;
	swap_config config;
} swap_config_wrapper;

swap_config_wrapper process_arguments(swap_config_wrapper config, int argc,
									  char **argv);
void print_usage();
void print_help(swap_config);


int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	char *status;
	status = strdup("");

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	swap_config_wrapper tmp = {.errorcode = OK};

	tmp.config = swap_config_init();

	tmp = process_arguments(tmp, argc, argv);

	if (tmp.errorcode != OK) {
		usage4(_("Could not parse arguments"));
	}

	swap_config config = tmp.config;

	swap_result data = get_swap_data(config);

	double percent_used;

	/* if total_swap_mb == 0, let's not divide by 0 */
	if (data.metrics.total != 0) {
		percent_used =
			100 * ((double)data.metrics.used) / ((double)data.metrics.total);
	} else {
		printf(_("SWAP %s - Swap is either disabled, not present, or of zero "
				 "size."),
			   state_text(data.statusCode));
		exit(config.no_swap_state);
	}

	if (config.verbose) {
		printf("Computed usage percentage: %g\n", percent_used);
	}

	uint64_t warn_print = config.warn.value;
	if (config.warn.is_percentage) {
		warn_print = config.warn.value * (data.metrics.total / 100);
	}

	uint64_t crit_print = config.crit.value;
	if (config.crit.is_percentage) {
		crit_print = config.crit.value * (data.metrics.total / 100);
	}

	char *perfdata =
		perfdata_uint64("swap", data.metrics.free, "B", true, warn_print, true,
						crit_print, true, 0, true, (long)data.metrics.total);

	if (config.verbose > 1) {
		printf("Warn threshold value: %" PRIu64 "\n", config.warn.value);
	}

	if ((config.warn.is_percentage &&
		 (percent_used >= (100 - config.warn.value))) ||
		config.warn.value >= data.metrics.free) {
		data.statusCode = max_state(data.statusCode, STATE_WARNING);
	}

	if (config.verbose > 1) {
		printf("Crit threshold value: %" PRIu64 "\n", config.crit.value);
	}

	if ((config.crit.is_percentage &&
		 (percent_used >= (100 - config.crit.value))) ||
		config.crit.value >= data.metrics.free) {
		data.statusCode = max_state(data.statusCode, STATE_CRITICAL);
	}

	printf(_("SWAP %s - %g%% free (%lluMB out of %lluMB) %s|%s\n"),
		   state_text(data.statusCode), (100 - percent_used), data.metrics.free,
		   data.metrics.total, status, perfdata);

	exit(data.statusCode);
}

/* process command-line arguments */
swap_config_wrapper process_arguments(swap_config_wrapper conf_wrapper,
									  int argc, char **argv) {
	if (argc < 2) {
		conf_wrapper.errorcode = ERROR;
		return conf_wrapper;
	}

	int option = 0;
	static struct option longopts[] = {{"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"allswaps", no_argument, 0, 'a'},
									   {"no-swap", required_argument, 0, 'n'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	int c = 0; /* option character */
	while (true) {
		c = getopt_long(argc, argv, "+?Vvhac:w:n:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w': /* warning size threshold */
		{
			/*
			 * We expect either a positive integer value without a unit, which
			 * means the unit is Bytes or a positive integer value and a
			 * percentage sign (%), which means the value must be with 0 and 100
			 * and is relative to the total swap
			 */
			size_t length;
			length = strlen(optarg);

			if (optarg[length - 1] == '%') {
				/* It's percentage */
				conf_wrapper.config.warn.is_percentage = true;
				optarg[length - 1] = '\0';
				if (is_uint64(optarg, &conf_wrapper.config.warn.value)) {
					if (conf_wrapper.config.warn.value > 100) {
						usage4(
							_("Warning threshold percentage must be <= 100!"));
					}
				}
				break;
			} else {
				/* It's Bytes */
				conf_wrapper.config.warn.is_percentage = false;
				if (is_uint64(optarg, &conf_wrapper.config.warn.value)) {
					break;
				} else {
					usage4(_("Warning threshold be positive integer or "
							 "percentage!"));
				}
			}
		}
		case 'c': /* critical size threshold */
		{
			/*
			 * We expect either a positive integer value without a unit, which
			 * means the unit is Bytes or a positive integer value and a
			 * percentage sign (%), which means the value must be with 0 and 100
			 * and is relative to the total swap
			 */
			size_t length;
			length = strlen(optarg);

			if (optarg[length - 1] == '%') {
				/* It's percentage */
				conf_wrapper.config.crit.is_percentage = true;
				optarg[length - 1] = '\0';
				if (is_uint64(optarg, &conf_wrapper.config.crit.value)) {
					if (conf_wrapper.config.crit.value > 100) {
						usage4(
							_("Critical threshold percentage must be <= 100!"));
					}
				}
				break;
			} else {
				/* It's Bytes */
				conf_wrapper.config.crit.is_percentage = false;
				if (is_uint64(optarg, &conf_wrapper.config.crit.value)) {
					break;
				} else {
					usage4(_("Critical threshold be positive integer or "
							 "percentage!"));
				}
			}
		}
		case 'a': /* all swap */
			conf_wrapper.config.allswaps = true;
			break;
		case 'n':
			if ((conf_wrapper.config.no_swap_state =
					 mp_translate_state(optarg)) == ERROR) {
				usage4(_("no-swap result must be a valid state name (OK, "
						 "WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			}
			break;
		case 'v': /* verbose */
			conf_wrapper.config.verbose++;
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help(conf_wrapper.config);
			exit(STATE_UNKNOWN);
		case '?': /* error */
			usage5();
		}
	}

	c = optind;

	if (conf_wrapper.config.warn.value == 0 &&
		conf_wrapper.config.crit.value == 0) {
		conf_wrapper.errorcode = ERROR;
		return conf_wrapper;
	} else if ((conf_wrapper.config.warn.is_percentage ==
				conf_wrapper.config.crit.is_percentage) &&
			   (conf_wrapper.config.warn.value <
				conf_wrapper.config.crit.value)) {
		/* This is NOT triggered if warn and crit are different units, e.g warn
		 * is percentage and crit is absolute. We cannot determine the condition
		 * at this point since we dont know the value of total swap yet
		 */
		usage4(_("Warning should be more than critical"));
	}

	return conf_wrapper;
}

void print_help(swap_config config) {
	print_revision(progname, NP_VERSION);

	printf(_(COPYRIGHT), copyright, email);

	printf("%s\n", _("Check swap space on local machine."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-w, --warning=INTEGER");
	printf("    %s\n", _("Exit with WARNING status if less than INTEGER bytes "
						 "of swap space are free"));
	printf(" %s\n", "-w, --warning=PERCENT%");
	printf("    %s\n", _("Exit with WARNING status if less than PERCENT of "
						 "swap space is free"));
	printf(" %s\n", "-c, --critical=INTEGER");
	printf("    %s\n", _("Exit with CRITICAL status if less than INTEGER bytes "
						 "of swap space are free"));
	printf(" %s\n", "-c, --critical=PERCENT%");
	printf("    %s\n", _("Exit with CRITICAL status if less than PERCENT of "
						 "swap space is free"));
	printf(" %s\n", "-a, --allswaps");
	printf("    %s\n",
		   _("Conduct comparisons for all swap partitions, one by one"));
	printf(" %s\n", "-n, --no-swap=<ok|warning|critical|unknown>");
	printf("    %s %s\n",
		   _("Resulting state when there is no swap regardless of thresholds. "
			 "Default:"),
		   state_text(config.no_swap_state));
	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("Both INTEGER and PERCENT thresholds can be specified, "
					  "they are all checked."));
	printf(
		" %s\n",
		_("On AIX, if -a is specified, uses lsps -a, otherwise uses lsps -s."));

	printf(UT_SUPPORT);
}

void print_usage() {
	printf("%s\n", _("Usage:"));
	printf(" %s [-av] -w <percent_free>%% -c <percent_free>%%\n", progname);
	printf("  -w <bytes_free> -c <bytes_free> [-n <state>]\n");
}
