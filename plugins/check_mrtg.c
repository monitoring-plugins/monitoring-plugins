/*****************************************************************************
 *
 * Monitoring check_mrtg plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_mrtg plugin
 *
 * This plugin will check either the average or maximum value of one of the
 * two variables recorded in an MRTG log file.
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

const char *progname = "check_mrtg";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "check_mrtg.d/config.h"

typedef struct {
	int errorcode;
	check_mrtg_config config;
} check_mrtg_config_wrapper;
static check_mrtg_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_mrtg_config_wrapper validate_arguments(check_mrtg_config_wrapper /*config_wrapper*/);

static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_mrtg_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments\n"));
	}

	const check_mrtg_config config = tmp_config.config;

	/* open the MRTG log file for reading */
	FILE *mtrg_log_file = fopen(config.log_file, "r");
	if (mtrg_log_file == NULL) {
		printf(_("Unable to open MRTG log file\n"));
		return STATE_UNKNOWN;
	}

	time_t timestamp = 0;
	unsigned long average_value_rate = 0;
	unsigned long maximum_value_rate = 0;
	char input_buffer[MAX_INPUT_BUFFER];
	int line = 0;
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, mtrg_log_file)) {
		line++;

		/* skip the first line of the log file */
		if (line == 1) {
			continue;
		}

		/* break out of read loop if we've passed the number of entries we want to read */
		if (line > 2) {
			break;
		}

		/* grab the timestamp */
		char *temp_buffer = strtok(input_buffer, " ");
		timestamp = strtoul(temp_buffer, NULL, 10);

		/* grab the average value 1 rate */
		temp_buffer = strtok(NULL, " ");
		if (config.variable_number == 1) {
			average_value_rate = strtoul(temp_buffer, NULL, 10);
		}

		/* grab the average value 2 rate */
		temp_buffer = strtok(NULL, " ");
		if (config.variable_number == 2) {
			average_value_rate = strtoul(temp_buffer, NULL, 10);
		}

		/* grab the maximum value 1 rate */
		temp_buffer = strtok(NULL, " ");
		if (config.variable_number == 1) {
			maximum_value_rate = strtoul(temp_buffer, NULL, 10);
		}

		/* grab the maximum value 2 rate */
		temp_buffer = strtok(NULL, " ");
		if (config.variable_number == 2) {
			maximum_value_rate = strtoul(temp_buffer, NULL, 10);
		}
	}

	/* close the log file */
	fclose(mtrg_log_file);

	/* if we couldn't read enough data, return an unknown error */
	if (line <= 2) {
		printf(_("Unable to process MRTG log file\n"));
		return STATE_UNKNOWN;
	}

	/* make sure the MRTG data isn't too old */
	time_t current_time;
	time(&current_time);
	if (config.expire_minutes > 0 && (current_time - timestamp) > (config.expire_minutes * 60)) {
		printf(_("MRTG data has expired (%d minutes old)\n"), (int)((current_time - timestamp) / 60));
		return STATE_WARNING;
	}

	unsigned long rate = 0L;
	/* else check the incoming/outgoing rates */
	if (config.use_average) {
		rate = average_value_rate;
	} else {
		rate = maximum_value_rate;
	}

	int result = STATE_OK;
	if (config.value_critical_threshold_set && rate > config.value_critical_threshold) {
		result = STATE_CRITICAL;
	} else if (config.value_warning_threshold_set && rate > config.value_warning_threshold) {
		result = STATE_WARNING;
	}

	printf("%s. %s = %lu %s|%s\n", (config.use_average) ? _("Avg") : _("Max"), config.label, rate, config.units,
		   perfdata(config.label, (long)rate, config.units, config.value_warning_threshold_set, (long)config.value_warning_threshold,
					config.value_critical_threshold_set, (long)config.value_critical_threshold, 0, 0, 0, 0));

	return result;
}

/* process command-line arguments */
check_mrtg_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {
		{"logfile", required_argument, 0, 'F'},  {"expires", required_argument, 0, 'e'},  {"aggregation", required_argument, 0, 'a'},
		{"variable", required_argument, 0, 'v'}, {"critical", required_argument, 0, 'c'}, {"warning", required_argument, 0, 'w'},
		{"label", required_argument, 0, 'l'},    {"units", required_argument, 0, 'u'},    {"variable", required_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},        {"help", no_argument, 0, 'h'},           {0, 0, 0, 0}};

	check_mrtg_config_wrapper result = {
		.errorcode = OK,
		.config = check_mrtg_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0) {
			strcpy(argv[i], "-t");
		} else if (strcmp("-wt", argv[i]) == 0) {
			strcpy(argv[i], "-w");
		} else if (strcmp("-ct", argv[i]) == 0) {
			strcpy(argv[i], "-c");
		}
	}

	int option_char;
	int option = 0;
	while (1) {
		option_char = getopt_long(argc, argv, "hVF:e:a:v:c:w:l:u:", longopts, &option);

		if (option_char == -1 || option_char == EOF) {
			break;
		}

		switch (option_char) {
		case 'F': /* input file */
			result.config.log_file = optarg;
			break;
		case 'e': /* ups name */
			result.config.expire_minutes = atoi(optarg);
			break;
		case 'a': /* port */
			result.config.use_average = (bool)(strcmp(optarg, "MAX"));
			break;
		case 'v':
			result.config.variable_number = atoi(optarg);
			if (result.config.variable_number < 1 || result.config.variable_number > 2) {
				usage4(_("Invalid variable number"));
			}
			break;
		case 'w': /* critical time threshold */
			result.config.value_warning_threshold_set = true;
			result.config.value_warning_threshold = strtoul(optarg, NULL, 10);
			break;
		case 'c': /* warning time threshold */
			result.config.value_critical_threshold_set = true;
			result.config.value_critical_threshold = strtoul(optarg, NULL, 10);
			break;
		case 'l': /* label */
			result.config.label = optarg;
			break;
		case 'u': /* timeout */
			result.config.units = optarg;
			break;
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case '?': /* help */
			usage5();
		}
	}

	option_char = optind;
	if (result.config.log_file == NULL && argc > option_char) {
		result.config.log_file = argv[option_char++];
	}

	if (result.config.expire_minutes <= 0 && argc > option_char) {
		if (is_intpos(argv[option_char])) {
			result.config.expire_minutes = atoi(argv[option_char++]);
		} else {
			die(STATE_UNKNOWN, _("%s is not a valid expiration time\nUse '%s -h' for additional help\n"), argv[option_char], progname);
		}
	}

	if (argc > option_char && strcmp(argv[option_char], "MAX") == 0) {
		result.config.use_average = false;
		option_char++;
	} else if (argc > option_char && strcmp(argv[option_char], "AVG") == 0) {
		result.config.use_average = true;
		option_char++;
	}

	if (argc > option_char && result.config.variable_number == -1) {
		result.config.variable_number = atoi(argv[option_char++]);
		if (result.config.variable_number < 1 || result.config.variable_number > 2) {
			printf("%s :", argv[option_char]);
			usage(_("Invalid variable number\n"));
		}
	}

	if (argc > option_char && !result.config.value_warning_threshold_set) {
		result.config.value_warning_threshold_set = true;
		result.config.value_warning_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	if (argc > option_char && !result.config.value_critical_threshold_set) {
		result.config.value_critical_threshold_set = true;
		result.config.value_critical_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	if (argc > option_char && strlen(result.config.label) == 0) {
		result.config.label = argv[option_char++];
	}

	if (argc > option_char && strlen(result.config.units) == 0) {
		result.config.units = argv[option_char++];
	}

	return validate_arguments(result);
}

check_mrtg_config_wrapper validate_arguments(check_mrtg_config_wrapper config_wrapper) {
	if (config_wrapper.config.variable_number == -1) {
		usage4(_("You must supply the variable number"));
	}

	if (config_wrapper.config.label == NULL) {
		config_wrapper.config.label = strdup("value");
	}

	if (config_wrapper.config.units == NULL) {
		config_wrapper.config.units = strdup("");
	}

	return config_wrapper;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will check either the average or maximum value of one of the"));
	printf("%s\n", _("two variables recorded in an MRTG log file."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-F, --logfile=FILE");
	printf("   %s\n", _("The MRTG log file containing the data you want to monitor"));
	printf(" %s\n", "-e, --expires=MINUTES");
	printf("   %s\n", _("Minutes before MRTG data is considered to be too old"));
	printf(" %s\n", "-a, --aggregation=AVG|MAX");
	printf("   %s\n", _("Should we check average or maximum values?"));
	printf(" %s\n", "-v, --variable=INTEGER");
	printf("   %s\n", _("Which variable set should we inspect? (1 or 2)"));
	printf(" %s\n", "-w, --warning=INTEGER");
	printf("   %s\n", _("Threshold value for data to result in WARNING status"));
	printf(" %s\n", "-c, --critical=INTEGER");
	printf("   %s\n", _("Threshold value for data to result in CRITICAL status"));
	printf(" %s\n", "-l, --label=STRING");
	printf("   %s\n", _("Type label for data (Examples: Conns, \"Processor Load\", In, Out)"));
	printf(" %s\n", "-u, --units=STRING");
	printf("   %s\n", _("Option units label for data (Example: Packets/Sec, Errors/Sec,"));
	printf("   %s\n", _("\"Bytes Per Second\", \"%% Utilization\")"));

	printf("\n");
	printf(" %s\n", _("If the value exceeds the <vwl> threshold, a WARNING status is returned. If"));
	printf(" %s\n", _("the value exceeds the <vcl> threshold, a CRITICAL status is returned.  If"));
	printf(" %s\n", _("the data in the log file is older than <expire_minutes> old, a WARNING"));
	printf(" %s\n", _("status is returned and a warning message is printed."));

	printf("\n");
	printf(" %s\n", _("This plugin is useful for monitoring MRTG data that does not correspond to"));
	printf(" %s\n", _("bandwidth usage.  (Use the check_mrtgtraf plugin for monitoring bandwidth)."));
	printf(" %s\n", _("It can be used to monitor any kind of data that MRTG is monitoring - errors,"));
	printf(" %s\n", _("packets/sec, etc.  I use MRTG in conjunction with the Novell NLM that allows"));
	printf(" %s\n", _("me to track processor utilization, user connections, drive space, etc and"));
	printf(" %s\n\n", _("this plugin works well for monitoring that kind of data as well."));

	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("- This plugin only monitors one of the two variables stored in the MRTG log"));
	printf("   %s\n", _("file.  If you want to monitor both values you will have to define two"));
	printf("   %s\n", _("commands with different values for the <variable> argument.  Of course,"));
	printf("   %s\n", _("you can always hack the code to make this plugin work for you..."));
	printf(" %s\n", _("- MRTG stands for the Multi Router Traffic Grapher.  It can be downloaded from"));
	printf("   %s\n", "http://ee-staff.ethz.ch/~oetiker/webtools/mrtg/mrtg.html");

	printf(UT_SUPPORT);
}

/* original command line:
	 <log_file> <expire_minutes> <AVG|MAX> <variable> <vwl> <vcl> <label> [units] */

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -F log_file -a <AVG | MAX> -v variable -w warning -c critical\n", progname);
	printf("[-l label] [-u units] [-e expire_minutes] [-t timeout] [-v]\n");
}
