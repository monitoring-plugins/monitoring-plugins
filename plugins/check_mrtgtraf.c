/*****************************************************************************
 *
 * Monitoring check_mrtgtraf plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_mtrgtraf plugin
 *
 * This plugin will check the incoming/outgoing transfer rates of a router
 * switch, etc recorded in an MRTG log.
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

#include "common.h"
#include "utils.h"

const char *progname = "check_mrtgtraf";
const char *copyright = "1999-2024";
const char *email = "devel@monitoring-plugins.org";

static int process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

static char *log_file = NULL;
static int expire_minutes = -1;
static bool use_average = true;
static unsigned long incoming_warning_threshold = 0L;
static unsigned long incoming_critical_threshold = 0L;
static unsigned long outgoing_warning_threshold = 0L;
static unsigned long outgoing_critical_threshold = 0L;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	if (process_arguments(argc, argv) == ERROR)
		usage4(_("Could not parse arguments"));

	/* open the MRTG log file for reading */
	FILE *mrtg_log_file_ptr = fopen(log_file, "r");
	if (mrtg_log_file_ptr == NULL)
		usage4(_("Unable to open MRTG log file"));

	time_t timestamp = 0L;
	char input_buffer[MAX_INPUT_BUFFER];
	unsigned long average_incoming_rate = 0L;
	unsigned long average_outgoing_rate = 0L;
	unsigned long maximum_incoming_rate = 0L;
	unsigned long maximum_outgoing_rate = 0L;
	int line = 0;
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, mrtg_log_file_ptr)) {

		line++;

		/* skip the first line of the log file */
		if (line == 1)
			continue;

		/* break out of read loop */
		/* if we've passed the number of entries we want to read */
		if (line > 2)
			break;

		/* grab the timestamp */
		char *temp_buffer = strtok(input_buffer, " ");
		timestamp = strtoul(temp_buffer, NULL, 10);

		/* grab the average incoming transfer rate */
		temp_buffer = strtok(NULL, " ");
		average_incoming_rate = strtoul(temp_buffer, NULL, 10);

		/* grab the average outgoing transfer rate */
		temp_buffer = strtok(NULL, " ");
		average_outgoing_rate = strtoul(temp_buffer, NULL, 10);

		/* grab the maximum incoming transfer rate */
		temp_buffer = strtok(NULL, " ");
		maximum_incoming_rate = strtoul(temp_buffer, NULL, 10);

		/* grab the maximum outgoing transfer rate */
		temp_buffer = strtok(NULL, " ");
		maximum_outgoing_rate = strtoul(temp_buffer, NULL, 10);
	}

	/* close the log file */
	fclose(mrtg_log_file_ptr);

	/* if we couldn't read enough data, return an unknown error */
	if (line <= 2)
		usage4(_("Unable to process MRTG log file"));

	/* make sure the MRTG data isn't too old */
	time_t current_time;
	time(&current_time);
	if ((expire_minutes > 0) && (current_time - timestamp) > (expire_minutes * 60))
		die(STATE_WARNING, _("MRTG data has expired (%d minutes old)\n"), (int)((current_time - timestamp) / 60));

	unsigned long incoming_rate = 0L;
	unsigned long outgoing_rate = 0L;
	/* else check the incoming/outgoing rates */
	if (use_average) {
		incoming_rate = average_incoming_rate;
		outgoing_rate = average_outgoing_rate;
	} else {
		incoming_rate = maximum_incoming_rate;
		outgoing_rate = maximum_outgoing_rate;
	}

	double adjusted_incoming_rate = 0.0;
	char incoming_speed_rating[8];
	/* report incoming traffic in Bytes/sec */
	if (incoming_rate < 1024) {
		strcpy(incoming_speed_rating, "B");
		adjusted_incoming_rate = (double)incoming_rate;
	}

	/* report incoming traffic in KBytes/sec */
	else if (incoming_rate < (1024 * 1024)) {
		strcpy(incoming_speed_rating, "KB");
		adjusted_incoming_rate = (double)(incoming_rate / 1024.0);
	}

	/* report incoming traffic in MBytes/sec */
	else {
		strcpy(incoming_speed_rating, "MB");
		adjusted_incoming_rate = (double)(incoming_rate / 1024.0 / 1024.0);
	}

	double adjusted_outgoing_rate = 0.0;
	char outgoing_speed_rating[8];
	/* report outgoing traffic in Bytes/sec */
	if (outgoing_rate < 1024) {
		strcpy(outgoing_speed_rating, "B");
		adjusted_outgoing_rate = (double)outgoing_rate;
	}

	/* report outgoing traffic in KBytes/sec */
	else if (outgoing_rate < (1024 * 1024)) {
		strcpy(outgoing_speed_rating, "KB");
		adjusted_outgoing_rate = (double)(outgoing_rate / 1024.0);
	}

	/* report outgoing traffic in MBytes/sec */
	else {
		strcpy(outgoing_speed_rating, "MB");
		adjusted_outgoing_rate = (double)(outgoing_rate / 1024.0 / 1024.0);
	}

	int result = STATE_OK;
	if (incoming_rate > incoming_critical_threshold || outgoing_rate > outgoing_critical_threshold) {
		result = STATE_CRITICAL;
	} else if (incoming_rate > incoming_warning_threshold || outgoing_rate > outgoing_warning_threshold) {
		result = STATE_WARNING;
	}

	char *error_message;
	xasprintf(&error_message, _("%s. In = %0.1f %s/s, %s. Out = %0.1f %s/s|%s %s\n"), (use_average) ? _("Avg") : _("Max"),
			  adjusted_incoming_rate, incoming_speed_rating, (use_average) ? _("Avg") : _("Max"), adjusted_outgoing_rate,
			  outgoing_speed_rating,
			  fperfdata("in", adjusted_incoming_rate, incoming_speed_rating, (int)incoming_warning_threshold, incoming_warning_threshold,
						(int)incoming_critical_threshold, incoming_critical_threshold, true, 0, false, 0),
			  fperfdata("out", adjusted_outgoing_rate, outgoing_speed_rating, (int)outgoing_warning_threshold, outgoing_warning_threshold,
						(int)outgoing_critical_threshold, outgoing_critical_threshold, true, 0, false, 0));

	printf(_("Traffic %s - %s\n"), state_text(result), error_message);

	return result;
}

/* process command-line arguments */
int process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"filename", required_argument, 0, 'F'},
									   {"expires", required_argument, 0, 'e'},
									   {"aggregation", required_argument, 0, 'a'},
									   {"critical", required_argument, 0, 'c'},
									   {"warning", required_argument, 0, 'w'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	if (argc < 2)
		return ERROR;

	for (int i = 1; i < argc; i++) {
		if (strcmp("-to", argv[i]) == 0)
			strcpy(argv[i], "-t");
		else if (strcmp("-wt", argv[i]) == 0)
			strcpy(argv[i], "-w");
		else if (strcmp("-ct", argv[i]) == 0)
			strcpy(argv[i], "-c");
	}

	int option_char;
	int option = 0;
	while (1) {
		option_char = getopt_long(argc, argv, "hVF:e:a:c:w:", longopts, &option);

		if (option_char == -1 || option_char == EOF)
			break;

		switch (option_char) {
		case 'F': /* input file */
			log_file = optarg;
			break;
		case 'e': /* expiration time */
			expire_minutes = atoi(optarg);
			break;
		case 'a': /* aggregation (AVE or MAX) */
			if (!strcmp(optarg, "MAX"))
				use_average = false;
			else
				use_average = true;
			break;
		case 'c': /* warning threshold */
			sscanf(optarg, "%lu,%lu", &incoming_critical_threshold, &outgoing_critical_threshold);
			break;
		case 'w': /* critical threshold */
			sscanf(optarg, "%lu,%lu", &incoming_warning_threshold, &outgoing_warning_threshold);
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
	if (argc > option_char && log_file == NULL) {
		log_file = argv[option_char++];
	}

	if (argc > option_char && expire_minutes == -1) {
		expire_minutes = atoi(argv[option_char++]);
	}

	if (argc > option_char && strcmp(argv[option_char], "MAX") == 0) {
		use_average = false;
		option_char++;
	} else if (argc > option_char && strcmp(argv[option_char], "AVG") == 0) {
		use_average = true;
		option_char++;
	}

	if (argc > option_char && incoming_warning_threshold == 0) {
		incoming_warning_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	if (argc > option_char && incoming_critical_threshold == 0) {
		incoming_critical_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	if (argc > option_char && outgoing_warning_threshold == 0) {
		outgoing_warning_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	if (argc > option_char && outgoing_critical_threshold == 0) {
		outgoing_critical_threshold = strtoul(argv[option_char++], NULL, 10);
	}

	return OK;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will check the incoming/outgoing transfer rates of a router,"));
	printf("%s\n", _("switch, etc recorded in an MRTG log.  If the newest log entry is older"));
	printf("%s\n", _("than <expire_minutes>, a WARNING status is returned. If either the"));
	printf("%s\n", _("incoming or outgoing rates exceed the <icl> or <ocl> thresholds (in"));
	printf("%s\n", _("Bytes/sec), a CRITICAL status results.  If either of the rates exceed"));
	printf("%s\n", _("the <iwl> or <owl> thresholds (in Bytes/sec), a WARNING status results."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-F, --filename=STRING");
	printf("    %s\n", _("File to read log from"));
	printf(" %s\n", "-e, --expires=INTEGER");
	printf("    %s\n", _("Minutes after which log expires"));
	printf(" %s\n", "-a, --aggregation=(AVG|MAX)");
	printf("    %s\n", _("Test average or maximum"));
	printf(" %s\n", "-w, --warning");
	printf("    %s\n", _("Warning threshold pair <incoming>,<outgoing>"));
	printf(" %s\n", "-c, --critical");
	printf("    %s\n", _("Critical threshold pair <incoming>,<outgoing>"));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("- MRTG stands for Multi Router Traffic Grapher. It can be downloaded from"));
	printf(" %s\n", "  http://ee-staff.ethz.ch/~oetiker/webtools/mrtg/mrtg.html");
	printf(" %s\n", _("- While MRTG can monitor things other than traffic rates, this"));
	printf(" %s\n", _("  plugin probably won't work with much else without modification."));
	printf(" %s\n", _("- The calculated i/o rates are a little off from what MRTG actually"));
	printf(" %s\n", _("  reports.  I'm not sure why this is right now, but will look into it"));
	printf(" %s\n", _("  for future enhancements of this plugin."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf(_("Usage"));
	printf(" %s -F <log_file> -a <AVG | MAX> -w <warning_pair>\n", progname);
	printf("-c <critical_pair> [-e expire_minutes]\n");
}
