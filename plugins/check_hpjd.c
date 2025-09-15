/*****************************************************************************
 *
 * Monitoring check_hpjd plugin
 *
 * License: GPL
 * Copyright (c) 2000-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_hpjd plugin
 *
 * This plugin tests the STATUS of an HP printer with a JetDirect card.
 * Net-SNMP must be installed on the computer running the plugin.
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

const char *progname = "check_hpjd";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "popen.h"
#include "utils.h"
#include "netutils.h"
#include "states.h"
#include "check_hpjd.d/config.h"

#define DEFAULT_COMMUNITY "public"

#define HPJD_LINE_STATUS           ".1.3.6.1.4.1.11.2.3.9.1.1.2.1"
#define HPJD_PAPER_STATUS          ".1.3.6.1.4.1.11.2.3.9.1.1.2.2"
#define HPJD_INTERVENTION_REQUIRED ".1.3.6.1.4.1.11.2.3.9.1.1.2.3"
#define HPJD_GD_PERIPHERAL_ERROR   ".1.3.6.1.4.1.11.2.3.9.1.1.2.6"
#define HPJD_GD_PAPER_OUT          ".1.3.6.1.4.1.11.2.3.9.1.1.2.8"
#define HPJD_GD_PAPER_JAM          ".1.3.6.1.4.1.11.2.3.9.1.1.2.9"
#define HPJD_GD_TONER_LOW          ".1.3.6.1.4.1.11.2.3.9.1.1.2.10"
#define HPJD_GD_PAGE_PUNT          ".1.3.6.1.4.1.11.2.3.9.1.1.2.11"
#define HPJD_GD_MEMORY_OUT         ".1.3.6.1.4.1.11.2.3.9.1.1.2.12"
#define HPJD_GD_DOOR_OPEN          ".1.3.6.1.4.1.11.2.3.9.1.1.2.17"
#define HPJD_GD_PAPER_OUTPUT       ".1.3.6.1.4.1.11.2.3.9.1.1.2.19"
#define HPJD_GD_STATUS_DISPLAY     ".1.3.6.1.4.1.11.2.3.9.1.1.3"

#define ONLINE  0
#define OFFLINE 1

typedef struct {
	int errorcode;
	check_hpjd_config config;
} check_hpjd_config_wrapper;
static check_hpjd_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_hpjd_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_hpjd_config config = tmp_config.config;

	char query_string[512];
	/* removed ' 2>1' at end of command 10/27/1999 - EG */
	/* create the query string */
	sprintf(query_string, "%s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0 %s.0",
			HPJD_LINE_STATUS, HPJD_PAPER_STATUS, HPJD_INTERVENTION_REQUIRED,
			HPJD_GD_PERIPHERAL_ERROR, HPJD_GD_PAPER_JAM, HPJD_GD_PAPER_OUT, HPJD_GD_TONER_LOW,
			HPJD_GD_PAGE_PUNT, HPJD_GD_MEMORY_OUT, HPJD_GD_DOOR_OPEN, HPJD_GD_PAPER_OUTPUT,
			HPJD_GD_STATUS_DISPLAY);

	/* get the command to run */
	char command_line[1024];
	sprintf(command_line, "%s -OQa -m : -v 1 -c %s %s:%u %s", PATH_TO_SNMPGET, config.community,
			config.address, config.port, query_string);

	/* run the command */
	child_process = spopen(command_line);
	if (child_process == NULL) {
		printf(_("Could not open pipe: %s\n"), command_line);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		printf(_("Could not open stderr for %s\n"), command_line);
	}

	mp_state_enum result = STATE_OK;

	int line_status = ONLINE;
	int paper_status = 0;
	int intervention_required = 0;
	int peripheral_error = 0;
	int paper_jam = 0;
	int paper_out = 0;
	int toner_low = 0;
	int page_punt = 0;
	int memory_out = 0;
	int door_open = 0;
	int paper_output = 0;
	char display_message[MAX_INPUT_BUFFER];

	char input_buffer[MAX_INPUT_BUFFER];
	char *errmsg = malloc(MAX_INPUT_BUFFER);
	int line = 0;

	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
		/* strip the newline character from the end of the input */
		if (input_buffer[strlen(input_buffer) - 1] == '\n') {
			input_buffer[strlen(input_buffer) - 1] = 0;
		}

		line++;

		char *temp_buffer = strtok(input_buffer, "=");
		temp_buffer = strtok(NULL, "=");

		if (temp_buffer == NULL && line < 13) {
			result = STATE_UNKNOWN;
			strcpy(errmsg, input_buffer);
		} else {
			switch (line) {
			case 1: /* 1st line should contain the line status */
				line_status = atoi(temp_buffer);
				break;
			case 2: /* 2nd line should contain the paper status */
				paper_status = atoi(temp_buffer);
				break;
			case 3: /* 3rd line should be intervention required */
				intervention_required = atoi(temp_buffer);
				break;
			case 4: /* 4th line should be peripheral error */
				peripheral_error = atoi(temp_buffer);
				break;
			case 5: /* 5th line should contain the paper jam status */
				paper_jam = atoi(temp_buffer);
				break;
			case 6: /* 6th line should contain the paper out status */
				paper_out = atoi(temp_buffer);
				break;
			case 7: /* 7th line should contain the toner low status */
				toner_low = atoi(temp_buffer);
				break;
			case 8: /* did data come too slow for engine */
				page_punt = atoi(temp_buffer);
				break;
			case 9: /* did we run out of memory */
				memory_out = atoi(temp_buffer);
				break;
			case 10: /* is there a door open */
				door_open = atoi(temp_buffer);
				break;
			case 11: /* is output tray full */
				paper_output = atoi(temp_buffer);
				break;
			case 12: /* display panel message */
				strcpy(display_message, temp_buffer + 1);
				break;
			default: /* fold multiline message */
				strncat(display_message, input_buffer,
						sizeof(display_message) - strlen(display_message) - 1);
			}
		}

		/* break out of the read loop if we encounter an error */
		if (result != STATE_OK) {
			break;
		}
	}

	/* WARNING if output found on stderr */
	if (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		result = max_state(result, STATE_WARNING);
		/* remove CRLF */
		if (input_buffer[strlen(input_buffer) - 1] == '\n') {
			input_buffer[strlen(input_buffer) - 1] = 0;
		}
		sprintf(errmsg, "%s", input_buffer);
	}

	/* close stderr */
	(void)fclose(child_stderr);

	/* close the pipe */
	if (spclose(child_process)) {
		result = max_state(result, STATE_WARNING);
	}

	/* if there wasn't any output, display an error */
	if (line == 0) {
		/* might not be the problem, but most likely is. */
		result = STATE_UNKNOWN;
		xasprintf(&errmsg, "%s : Timeout from host %s\n", errmsg, config.address);
	}

	/* if we had no read errors, check the printer status results... */
	if (result == STATE_OK) {

		if (paper_jam) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Paper Jam"));
		} else if (paper_out) {
			if (config.check_paper_out) {
				result = STATE_WARNING;
			}
			strcpy(errmsg, _("Out of Paper"));
		} else if (line_status == OFFLINE) {
			if (strcmp(errmsg, "POWERSAVE ON") != 0) {
				result = STATE_WARNING;
				strcpy(errmsg, _("Printer Offline"));
			}
		} else if (peripheral_error) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Peripheral Error"));
		} else if (intervention_required) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Intervention Required"));
		} else if (toner_low) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Toner Low"));
		} else if (memory_out) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Insufficient Memory"));
		} else if (door_open) {
			result = STATE_WARNING;
			strcpy(errmsg, _("A Door is Open"));
		} else if (paper_output) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Output Tray is Full"));
		} else if (page_punt) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Data too Slow for Engine"));
		} else if (paper_status) {
			result = STATE_WARNING;
			strcpy(errmsg, _("Unknown Paper Error"));
		}
	}

	if (result == STATE_OK) {
		printf(_("Printer ok - (%s)\n"), display_message);
	} else if (result == STATE_UNKNOWN) {
		printf("%s\n", errmsg);
		/* if printer could not be reached, escalate to critical */
		if (strstr(errmsg, "Timeout")) {
			result = STATE_CRITICAL;
		}
	} else if (result == STATE_WARNING) {
		printf("%s (%s)\n", errmsg, display_message);
	}

	exit(result);
}

/* process command-line arguments */
check_hpjd_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"community", required_argument, 0, 'C'},
									   /*  		{"critical",       required_argument,0,'c'}, */
									   /*  		{"warning",        required_argument,0,'w'}, */
									   {"port", required_argument, 0, 'p'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {0, 0, 0, 0}};

	check_hpjd_config_wrapper result = {
		.errorcode = OK,
		.config = check_hpjd_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	int option = 0;
	while (true) {
		int option_index = getopt_long(argc, argv, "+hVH:C:p:D", longopts, &option);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case 'H': /* hostname */
			if (is_host(optarg)) {
				result.config.address = strscpy(result.config.address, optarg);
			} else {
				usage2(_("Invalid hostname/address"), optarg);
			}
			break;
		case 'C': /* community */
			result.config.community = strscpy(result.config.community, optarg);
			break;
		case 'p':
			if (!is_intpos(optarg)) {
				usage2(_("Port must be a positive short integer"), optarg);
			} else {
				result.config.port = atoi(optarg);
			}
			break;
		case 'D': /* disable paper out check*/
			result.config.check_paper_out = false;
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

	int c = optind;
	if (result.config.address == NULL) {
		if (is_host(argv[c])) {
			result.config.address = argv[c++];
		} else {
			usage2(_("Invalid hostname/address"), argv[c]);
		}
	}

	if (result.config.community == NULL) {
		if (argv[c] != NULL) {
			result.config.community = argv[c];
		} else {
			result.config.community = strdup(DEFAULT_COMMUNITY);
		}
	}

	return result;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the STATUS of an HP printer with a JetDirect card."));
	printf("%s\n", _("Net-snmp must be installed on the computer running the plugin."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-C, --community=STRING");
	printf("    %s", _("The SNMP community name "));
	printf(_("(default=%s)"), DEFAULT_COMMUNITY);
	printf("\n");
	printf(" %s\n", "-p, --port=STRING");
	printf("    %s", _("Specify the port to check "));
	printf(_("(default=%s)"), DEFAULT_PORT);
	printf("\n");
	printf(" %s\n", "-D");
	printf("    %s", _("Disable paper check "));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host [-C community] [-p port] [-D]\n", progname);
}
