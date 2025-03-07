/*****************************************************************************
 *
 * Monitoring check_game plugin
 *
 * License: GPL
 * Copyright (c) 2002-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_game plugin
 *
 * This plugin tests game server connections with the specified host.
 * using the qstat program
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

const char *progname = "check_game";
const char *copyright = "2002-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "runcmd.h"
#include "check_game.d/config.h"
#include "../lib/monitoringplug.h"

typedef struct {
	int errorcode;
	check_game_config config;
} check_game_config_wrapper;

static check_game_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

#define QSTAT_DATA_DELIMITER ","

#define QSTAT_HOST_ERROR      "ERROR"
#define QSTAT_HOST_DOWN       "DOWN"
#define QSTAT_HOST_TIMEOUT    "TIMEOUT"
#define QSTAT_MAX_RETURN_ARGS 12

static bool verbose = false;

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_game_config_wrapper tmp = process_arguments(argc, argv);

	if (tmp.errorcode == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	check_game_config config = tmp.config;

	mp_state_enum result = STATE_OK;

	/* create the command line to execute */
	char *command_line = NULL;
	xasprintf(&command_line, "%s -raw %s -%s %s", PATH_TO_QSTAT, QSTAT_DATA_DELIMITER, config.game_type, config.server_ip);

	if (config.port) {
		xasprintf(&command_line, "%s:%-d", command_line, config.port);
	}

	if (verbose) {
		printf("%s\n", command_line);
	}

	/* run the command. historically, this plugin ignores output on stderr,
	 * as well as return status of the qstat program */
	output chld_out = {};
	(void)np_runcmd(command_line, &chld_out, NULL, 0);

	/* sanity check */
	/* was thinking about running qstat without any options, capturing the
	   -default line, parsing it & making an array of all know server types
	   but thought this would be too much hassle considering this is a tool
	   for intelligent sysadmins (ha). Could put a static array of known
	   server types in a header file but then we'd be limiting ourselves

	   In the end, I figured I'd simply let an error occur & then trap it
	 */

	if (!strncmp(chld_out.line[0], "unknown option", strlen("unknown option"))) {
		printf(_("CRITICAL - Host type parameter incorrect!\n"));
		result = STATE_CRITICAL;
		exit(result);
	}

	char *ret[QSTAT_MAX_RETURN_ARGS];
	size_t i = 0;
	char *sequence = strtok(chld_out.line[0], QSTAT_DATA_DELIMITER);
	while (sequence != NULL) {
		ret[i] = sequence;
		sequence = strtok(NULL, QSTAT_DATA_DELIMITER);
		i++;
		if (i >= QSTAT_MAX_RETURN_ARGS) {
			break;
		}
	}

	if (strstr(ret[2], QSTAT_HOST_ERROR)) {
		printf(_("CRITICAL - Host not found\n"));
		result = STATE_CRITICAL;
	} else if (strstr(ret[2], QSTAT_HOST_DOWN)) {
		printf(_("CRITICAL - Game server down or unavailable\n"));
		result = STATE_CRITICAL;
	} else if (strstr(ret[2], QSTAT_HOST_TIMEOUT)) {
		printf(_("CRITICAL - Game server timeout\n"));
		result = STATE_CRITICAL;
	} else {
		printf("OK: %s/%s %s (%s), Ping: %s ms|%s %s\n", ret[config.qstat_game_players], ret[config.qstat_game_players_max],
			   ret[config.qstat_game_field], ret[config.qstat_map_field], ret[config.qstat_ping_field],
			   perfdata("players", atol(ret[config.qstat_game_players]), "", false, 0, false, 0, true, 0, true,
						atol(ret[config.qstat_game_players_max])),
			   fperfdata("ping", strtod(ret[config.qstat_ping_field], NULL), "", false, 0, false, 0, true, 0, false, 0));
	}

	exit(result);
}

#define players_field_index     129
#define max_players_field_index 130

check_game_config_wrapper process_arguments(int argc, char **argv) {
	static struct option long_opts[] = {{"help", no_argument, 0, 'h'},
										{"version", no_argument, 0, 'V'},
										{"verbose", no_argument, 0, 'v'},
										{"timeout", required_argument, 0, 't'},
										{"hostname", required_argument, 0, 'H'},
										{"port", required_argument, 0, 'P'},
										{"game-type", required_argument, 0, 'G'},
										{"map-field", required_argument, 0, 'm'},
										{"ping-field", required_argument, 0, 'p'},
										{"game-field", required_argument, 0, 'g'},
										{"players-field", required_argument, 0, players_field_index},
										{"max-players-field", required_argument, 0, max_players_field_index},
										{0, 0, 0, 0}};

	check_game_config_wrapper result = {
		.config = check_game_config_init(),
		.errorcode = OK,
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	for (int option_counter = 1; option_counter < argc; option_counter++) {
		if (strcmp("-mf", argv[option_counter]) == 0) {
			strcpy(argv[option_counter], "-m");
		} else if (strcmp("-pf", argv[option_counter]) == 0) {
			strcpy(argv[option_counter], "-p");
		} else if (strcmp("-gf", argv[option_counter]) == 0) {
			strcpy(argv[option_counter], "-g");
		}
	}

	int opt_index = 0;
	while (true) {
		int option_index = getopt_long(argc, argv, "hVvt:H:P:G:g:p:m:", long_opts, &opt_index);

		if (option_index == -1 || option_index == EOF) {
			break;
		}

		switch (option_index) {
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v': /* version */
			verbose = true;
			break;
		case 't': /* timeout period */
			timeout_interval = atoi(optarg);
			break;
		case 'H': /* hostname */
			if (strlen(optarg) >= MAX_HOST_ADDRESS_LENGTH) {
				die(STATE_UNKNOWN, _("Input buffer overflow\n"));
			}
			result.config.server_ip = optarg;
			break;
		case 'P': /* port */
			result.config.port = atoi(optarg);
			break;
		case 'G': /* hostname */
			if (strlen(optarg) >= MAX_INPUT_BUFFER) {
				die(STATE_UNKNOWN, _("Input buffer overflow\n"));
			}
			result.config.game_type = optarg;
			break;
		case 'p': /* index of ping field */
			result.config.qstat_ping_field = atoi(optarg);
			if (result.config.qstat_ping_field < 0 || result.config.qstat_ping_field > QSTAT_MAX_RETURN_ARGS) {
				result.errorcode = ERROR;
				return result;
			}
			break;
		case 'm': /* index on map field */
			result.config.qstat_map_field = atoi(optarg);
			if (result.config.qstat_map_field < 0 || result.config.qstat_map_field > QSTAT_MAX_RETURN_ARGS) {
				result.errorcode = ERROR;
				return result;
			}
			break;
		case 'g': /* index of game field */
			result.config.qstat_game_field = atoi(optarg);
			if (result.config.qstat_game_field < 0 || result.config.qstat_game_field > QSTAT_MAX_RETURN_ARGS) {
				result.errorcode = ERROR;
				return result;
			}
			break;
		case players_field_index: /* index of player count field */
			result.config.qstat_game_players = atoi(optarg);
			if (result.config.qstat_game_players_max == 0) {
				result.config.qstat_game_players_max = result.config.qstat_game_players - 1;
			}
			if (result.config.qstat_game_players < 0 || result.config.qstat_game_players > QSTAT_MAX_RETURN_ARGS) {
				result.errorcode = ERROR;
				return result;
			}
			break;
		case max_players_field_index: /* index of max players field */
			result.config.qstat_game_players_max = atoi(optarg);
			if (result.config.qstat_game_players_max < 0 || result.config.qstat_game_players_max > QSTAT_MAX_RETURN_ARGS) {
				result.errorcode = ERROR;
				return result;
			}
			break;
		default: /* args not parsable */
			usage5();
		}
	}

	int option_counter = optind;
	/* first option is the game type */
	if (!result.config.game_type && option_counter < argc) {
		result.config.game_type = strdup(argv[option_counter++]);
	}

	/* Second option is the server name */
	if (!result.config.server_ip && option_counter < argc) {
		result.config.server_ip = strdup(argv[option_counter++]);
	}

	return result;
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ian Cass, Knowledge Matters Limited\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin tests game server connections with the specified host."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);
	printf(" -H, --hostname=ADDRESS\n"
		  "    Host name, IP Address, or unix socket (must be an absolute path)\n");
	printf(" %s\n", "-P");
	printf("    %s\n", _("Optional port to connect to"));
	printf(" %s\n", "-g");
	printf("    %s\n", _("Field number in raw qstat output that contains game name"));
	printf(" %s\n", "-m");
	printf("    %s\n", _("Field number in raw qstat output that contains map name"));
	printf(" %s\n", "-p");
	printf("    %s\n", _("Field number in raw qstat output that contains ping time"));

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("This plugin uses the 'qstat' command, the popular game server status query tool."));
	printf(" %s\n", _("If you don't have the package installed, you will need to download it from"));
	printf(" %s\n", _("https://github.com/multiplay/qstat before you can use this plugin."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s [-hvV] [-P port] [-t timeout] [-g game_field] [-m map_field] [-p ping_field] [-G game-time] [-H hostname] <game> "
		   "<ip_address>\n",
		   progname);
}

/******************************************************************************
 *
 * Test Cases:
 *
 * ./check_game --players 7 -p 8 --map 5 qs 67.20.190.61 26000
 *
 * qstat -raw , -qs 67.20.190.61
 *  ==> QS,67.20.190.61,Nightmare.fintek.ca,67.20.190.61:26000,3,e2m1,6,0,83,0
 *
 * qstat -qs 67.20.190.61
 *  ==> ADDRESS           PLAYERS      MAP   RESPONSE TIME    NAME
 *  ==> 67.20.190.61            0/ 6     e2m1     79 / 0   Nightmare.fintek.ca
 *
 ******************************************************************************/
