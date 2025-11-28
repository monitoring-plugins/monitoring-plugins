/*****************************************************************************
 *
 * Monitoring check_dig plugin
 *
 * License: GPL
 * Copyright (c) 2002-2025 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_dig plugin
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

/* Hackers note:
 *  There are typecasts to (char *) from _("foo bar") in this file.
 *  They prevent compiler warnings. Never (ever), permute strings obtained
 *  that are typecast from (const char *) (which happens when --disable-nls)
 *  because on some architectures those strings are in non-writable memory */

const char *progname = "check_dig";
const char *copyright = "2002-2025";
const char *email = "devel@monitoring-plugins.org";

#include <ctype.h>
#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "runcmd.h"

#include "check_dig.d/config.h"
#include "states.h"

typedef struct {
	int errorcode;
	check_dig_config config;
} check_dig_config_wrapper;
static check_dig_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static check_dig_config_wrapper validate_arguments(check_dig_config_wrapper /*config_wrapper*/);

static void print_help(void);
void print_usage(void);

static int verbose = 0;

/* helpers for flag parsing */
static flag_list parse_flags_line(const char *line);
static flag_list split_csv_trim(const char *csv);
static bool flag_list_contains(const flag_list *list, const char *needle);
static void free_flag_list(flag_list *list);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Set signal handling and alarm */
	if (signal(SIGALRM, runcmd_timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_dig_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	const check_dig_config config = tmp_config.config;

	/* dig applies the timeout to each try, so we need to work around this */
	int timeout_interval_dig = ((int)timeout_interval / config.number_tries) + config.number_tries;

	char *command_line;
	/* get the command to run */
	xasprintf(&command_line, "%s %s %s -p %d @%s %s %s +retry=%d +time=%d", PATH_TO_DIG,
			  config.dig_args, config.query_transport, config.server_port, config.dns_server,
			  config.query_address, config.record_type, config.number_tries, timeout_interval_dig);

	alarm(timeout_interval);
	struct timeval start_time;
	gettimeofday(&start_time, NULL);

	if (verbose) {
		printf("%s\n", command_line);
		if (config.expected_address != NULL) {
			printf(_("Looking for: '%s'\n"), config.expected_address);
		} else {
			printf(_("Looking for: '%s'\n"), config.query_address);
		}
	}

	output chld_out;
	output chld_err;
	char *msg = NULL;
	flag_list dig_flags = {.items = NULL, .count = 0};
	mp_state_enum result = STATE_UNKNOWN;

	/* run the command */
	if (np_runcmd(command_line, &chld_out, &chld_err, 0) != 0) {
		result = STATE_WARNING;
		msg = (char *)_("dig returned an error status");
	}

	/* extract ';; flags: ...' from stdout (first occurrence) */
	for (size_t i = 0; i < chld_out.lines; i++) {
		if (strstr(chld_out.line[i], "flags:")) {
			if (verbose) {
				printf("Raw flags line: %s\n", chld_out.line[i]);
			}

			dig_flags = parse_flags_line(chld_out.line[i]);

			if (verbose && dig_flags.count > 0) {
				printf(_("Parsed flags:"));
				for (size_t k = 0; k < dig_flags.count; k++) {
					printf(" %s", dig_flags.items[k]);
				}
				printf("\n");
			}
			break;
		}
	}

	for (size_t i = 0; i < chld_out.lines; i++) {
		/* the server is responding, we just got the host name... */
		if (strstr(chld_out.line[i], ";; ANSWER SECTION:")) {

			/* loop through the whole 'ANSWER SECTION' */
			for (; i < chld_out.lines; i++) {
				/* get the host address */
				if (verbose) {
					printf("%s\n", chld_out.line[i]);
				}

				if (strcasestr(chld_out.line[i], (config.expected_address == NULL
													  ? config.query_address
													  : config.expected_address)) != NULL) {
					msg = chld_out.line[i];
					result = STATE_OK;

					/* Translate output TAB -> SPACE */
					char *temp = msg;
					while ((temp = strchr(temp, '\t')) != NULL) {
						*temp = ' ';
					}
					break;
				}
			}

			if (result == STATE_UNKNOWN) {
				msg = (char *)_("Server not found in ANSWER SECTION");
				result = STATE_WARNING;
			}

			/* we found the answer section, so break out of the loop */
			break;
		}
	}

	if (result == STATE_UNKNOWN) {
		msg = (char *)_("No ANSWER SECTION found");
		result = STATE_CRITICAL;
	}

	/* If we get anything on STDERR, at least set warning */
	if (chld_err.buflen > 0) {
		result = max_state(result, STATE_WARNING);
		if (!msg) {
			for (size_t i = 0; i < chld_err.lines; i++) {
				msg = strchr(chld_err.line[0], ':');
				if (msg) {
					msg++;
					break;
				}
			}
		}
	}

	long microsec = deltime(start_time);
	double elapsed_time = (double)microsec / 1.0e6;

	if (config.critical_interval > UNDEFINED && elapsed_time > config.critical_interval) {
		result = STATE_CRITICAL;
	}

	else if (config.warning_interval > UNDEFINED && elapsed_time > config.warning_interval) {
		result = STATE_WARNING;
	}

	/* Optional: evaluate dig flags only if -E/-X were provided */
	if ((config.require_flags.count > 0) || (config.forbid_flags.count > 0)) {
		if (dig_flags.count > 0) {
			for (size_t r = 0; r < config.require_flags.count; r++) {
				if (!flag_list_contains(&dig_flags, config.require_flags.items[r])) {
					result = STATE_CRITICAL;
					if (!msg) {
						xasprintf(&msg, _("Missing required DNS flag: %s"),
								  config.require_flags.items[r]);
					} else {
						char *newmsg = NULL;
						xasprintf(&newmsg, _("%s; missing required DNS flag: %s"), msg,
								  config.require_flags.items[r]);
						msg = newmsg;
					}
				}
			}

			for (size_t r = 0; r < config.forbid_flags.count; r++) {
				if (flag_list_contains(&dig_flags, config.forbid_flags.items[r])) {
					result = STATE_CRITICAL;
					if (!msg) {
						xasprintf(&msg, _("Forbidden DNS flag present: %s"),
								  config.forbid_flags.items[r]);
					} else {
						char *newmsg = NULL;
						xasprintf(&newmsg, _("%s; forbidden DNS flag present: %s"), msg,
								  config.forbid_flags.items[r]);
						msg = newmsg;
					}
				}
			}
		}
	}

	/* cleanup flags buffer */
	free_flag_list(&dig_flags);

	printf("DNS %s - %.3f seconds response time (%s)|%s\n", state_text(result), elapsed_time,
		   msg ? msg : _("Probably a non-existent host/domain"),
		   fperfdata("time", elapsed_time, "s", (config.warning_interval > UNDEFINED),
					 config.warning_interval, (config.critical_interval > UNDEFINED),
					 config.critical_interval, true, 0, false, 0));
	exit(result);
}

/* process command-line arguments */
check_dig_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"hostname", required_argument, 0, 'H'},
									   {"query_address", required_argument, 0, 'l'},
									   {"warning", required_argument, 0, 'w'},
									   {"critical", required_argument, 0, 'c'},
									   {"timeout", required_argument, 0, 't'},
									   {"dig-arguments", required_argument, 0, 'A'},
									   {"require-flags", required_argument, 0, 'E'},
									   {"forbid-flags", required_argument, 0, 'X'},
									   {"verbose", no_argument, 0, 'v'},
									   {"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"record_type", required_argument, 0, 'T'},
									   {"expected_address", required_argument, 0, 'a'},
									   {"port", required_argument, 0, 'p'},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {0, 0, 0, 0}};

	check_dig_config_wrapper result = {
		.errorcode = OK,
		.config = check_dig_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	int option = 0;
	while (true) {
		int option_index =
			getopt_long(argc, argv, "hVvt:l:H:w:c:T:p:a:A:E:X:46", longopts, &option);

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
		case 'H': /* hostname */
			host_or_die(optarg);
			result.config.dns_server = optarg;
			break;
		case 'p': /* server port */
			if (is_intpos(optarg)) {
				result.config.server_port = atoi(optarg);
			} else {
				usage_va(_("Port must be a positive integer - %s"), optarg);
			}
			break;
		case 'l': /* address to lookup */
			result.config.query_address = optarg;
			break;
		case 'w': /* warning */
			if (is_nonnegative(optarg)) {
				result.config.warning_interval = strtod(optarg, NULL);
			} else {
				usage_va(_("Warning interval must be a positive integer - %s"), optarg);
			}
			break;
		case 'c': /* critical */
			if (is_nonnegative(optarg)) {
				result.config.critical_interval = strtod(optarg, NULL);
			} else {
				usage_va(_("Critical interval must be a positive integer - %s"), optarg);
			}
			break;
		case 't': /* timeout */
			if (is_intnonneg(optarg)) {
				timeout_interval = atoi(optarg);
			} else {
				usage_va(_("Timeout interval must be a positive integer - %s"), optarg);
			}
			break;
		case 'A': /* dig arguments */
			result.config.dig_args = strdup(optarg);
			break;
		case 'E': /* require flags */
			result.config.require_flags = split_csv_trim(optarg);
			break;
		case 'X': /* forbid flags */
			result.config.forbid_flags = split_csv_trim(optarg);
			break;
		case 'v': /* verbose */
			verbose++;
			break;
		case 'T':
			result.config.record_type = optarg;
			break;
		case 'a':
			result.config.expected_address = optarg;
			break;
		case '4':
			result.config.query_transport = "-4";
			break;
		case '6':
			result.config.query_transport = "-6";
			break;
		default: /* usage5 */
			usage5();
		}
	}

	int index = optind;
	if (result.config.dns_server == NULL) {
		if (index < argc) {
			host_or_die(argv[index]);
			result.config.dns_server = argv[index];
		} else {
			if (strcmp(result.config.query_transport, "-6") == 0) {
				result.config.dns_server = strdup("::1");
			} else {
				result.config.dns_server = strdup("127.0.0.1");
			}
		}
	}

	return validate_arguments(result);
}

check_dig_config_wrapper validate_arguments(check_dig_config_wrapper config_wrapper) {
	if (config_wrapper.config.query_address == NULL) {
		config_wrapper.errorcode = ERROR;
	}
	return config_wrapper;
}

void print_help(void) {
	char *myport;

	xasprintf(&myport, "%d", DEFAULT_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 2000 Karl DeBisschop <kdebisschop@users.sourceforge.net>\n");
	printf(COPYRIGHT, copyright, email);

	printf(_("This plugin tests the DNS service on the specified host using dig"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);

	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'p', myport);

	printf(" %s\n", "-4, --use-ipv4");
	printf("    %s\n", _("Force dig to only use IPv4 query transport"));
	printf(" %s\n", "-6, --use-ipv6");
	printf("    %s\n", _("Force dig to only use IPv6 query transport"));
	printf(" %s\n", "-l, --query_address=STRING");
	printf("    %s\n", _("Machine name to lookup"));
	printf(" %s\n", "-T, --record_type=STRING");
	printf("    %s\n", _("Record type to lookup (default: A)"));
	printf(" %s\n", "-a, --expected_address=STRING");
	printf("    %s\n",
		   _("An address expected to be in the answer section. If not set, uses whatever"));
	printf("    %s\n", _("was in -l"));
	printf(" %s\n", "-A, --dig-arguments=STRING");
	printf("    %s\n", _("Pass STRING as argument(s) to dig"));
	printf(" %s\n", "-E, --require-flags=LIST");
	printf("    %s\n", _("Comma-separated dig flags that must be present (e.g. 'aa,qr')"));
	printf(" %s\n", "-X, --forbid-flags=LIST");
	printf("    %s\n", _("Comma-separated dig flags that must NOT be present"));
	printf(UT_WARN_CRIT);
	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", "check_dig -H DNSSERVER -l www.example.com -A \"+tcp\"");
	printf(" %s\n", "This will send a tcp query to DNSSERVER for www.example.com");

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -l <query_address> [-H <host>] [-p <server port>]\n", progname);
	printf(" [-T <query type>] [-w <warning interval>] [-c <critical interval>]\n");
	printf(" [-t <timeout>] [-a <expected answer address>] [-E <flags>] [-X <flags>] [-v]\n");
}

/* helpers */

/**
 * parse_flags_line - Parse a dig output line and extract DNS header flags.
 *
 * Input:
 *   line - NUL terminated dig output line, e.g. ";; flags: qr rd ra; ..."
 *
 * Returns:
 *   flag_list where:
 *     - items: array of NUL terminated flag strings (heap allocated)
 *     - count: number of entries in items
 *   On parse failure or if no flags were found, count is 0 and items is NULL.
 */
static flag_list parse_flags_line(const char *line) {
	flag_list result = {.items = NULL, .count = 0};

	if (!line) {
		return result;
	}

	/* Locate start of DNS header flags in dig output */
	const char *p = strstr(line, "flags:");
	if (!p) {
		return result;
	}
	p += 6; /* skip literal "flags:" */

	/* Skip whitespace after "flags:" */
	while (*p && isspace((unsigned char)*p)) {
		p++;
	}

	/* Flags are terminated by the next semicolon e.g. "qr rd ra;" */
	const char *q = strchr(p, ';');
	if (!q) {
		return result;
	}

	/* Extract substring containing the flag block */
	size_t len = (size_t)(q - p);
	if (len == 0) {
		return result;
	}

	char *buf = (char *)malloc(len + 1);
	if (!buf) {
		return result;
	}
	memcpy(buf, p, len);
	buf[len] = '\0';

	/* Tokenize flags separated by whitespace */
	char **arr = NULL;
	size_t cnt = 0;
	char *saveptr = NULL;
	char *tok = strtok_r(buf, " \t", &saveptr);

	while (tok) {
		/* Expand array for the next flag token */
		char **tmp = (char **)realloc(arr, (cnt + 1) * sizeof(char *));
		if (!tmp) {
			/* On allocation failure keep what we have and return it */
			break;
		}
		arr = tmp;
		arr[cnt++] = strdup(tok);
		tok = strtok_r(NULL, " \t", &saveptr);
	}

	free(buf);

	result.items = arr;
	result.count = cnt;
	return result;
}

/**
 * split_csv_trim - Split a comma separated string into trimmed tokens.
 *
 * Input:
 *   csv - NUL terminated string, e.g. "aa, qr , rd"
 *
 * Returns:
 *   flag_list where:
 *     - items: array of NUL terminated tokens (heap allocated, whitespace trimmed)
 *     - count: number of tokens
 *   On empty input, count is 0 and items is NULL
 */
static flag_list split_csv_trim(const char *csv) {
	flag_list result = {.items = NULL, .count = 0};

	if (!csv || !*csv) {
		return result;
	}

	char *tmp = strdup(csv);
	if (!tmp) {
		return result;
	}

	char *s = tmp;
	char *token = NULL;

	/* Split CSV by commas, trimming whitespace on each token */
	while ((token = strsep(&s, ",")) != NULL) {
		/* trim leading whitespace */
		while (*token && isspace((unsigned char)*token)) {
			token++;
		}

		/* trim trailing whitespace */
		char *end = token + strlen(token);
		while (end > token && isspace((unsigned char)end[-1])) {
			*--end = '\0';
		}

		if (*token) {
			/* Expand the items array and append the token */
			char **arr = (char **)realloc(result.items, (result.count + 1) * sizeof(char *));
			if (!arr) {
				/* Allocation failed, stop and return what we have */
				break;
			}
			result.items = arr;
			result.items[result.count++] = strdup(token);
		}
	}

	free(tmp);
	return result;
}

/**
 * flag_list_contains - Case-insensitive membership test in a flag_list.
 *
 * Input:
 *   list   - pointer to a flag_list
 *   needle - NUL terminated string to search for
 *
 * Returns:
 *   true  if needle is contained in list (strcasecmp)
 *   false otherwise
 */
static bool flag_list_contains(const flag_list *list, const char *needle) {
	if (!list || !needle || !*needle) {
		return false;
	}

	for (size_t i = 0; i < list->count; i++) {
		if (strcasecmp(list->items[i], needle) == 0) {
			return true;
		}
	}
	return false;
}

/**
 * free_flag_list - Release all heap allocations held by a flag_list.
 *
 * Input:
 *   list - pointer to a flag_list whose items were allocated by
 *          parse_flags_line() or split_csv_trim().
 *
 * After this call list->items is NULL and list->count is 0.
 */
static void free_flag_list(flag_list *list) {
	if (!list || !list->items) {
		return;
	}

	for (size_t i = 0; i < list->count; i++) {
		free(list->items[i]);
	}
	free(list->items);

	list->items = NULL;
	list->count = 0;
}
