/*****************************************************************************
 *
 * Monitoring check_apt plugin
 *
 * License: GPL
 * Copyright (c) 2006-2024 Monitoring Plugins Development Team
 *
 * Original author: Sean Finney
 *
 * Description:
 *
 * This file contains the check_apt plugin
 *
 * Check for available updates in apt package management systems
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
 *****************************************************************************/

#include "states.h"
const char *progname = "check_apt";
const char *copyright = "2006-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "runcmd.h"
#include "utils.h"
#include "regex.h"
#include "check_apt.d/config.h"

/* Character for hidden input file option (for testing). */
#define INPUT_FILE_OPT CHAR_MAX + 1
/* the default opts can be overridden via the cmdline */
#define UPGRADE_DEFAULT_OPTS "-o 'Debug::NoLocking=true' -s -qq"
#define UPDATE_DEFAULT_OPTS  "-q"
/* until i commit the configure.in patch which gets this, i'll define
 * it here as well */
#ifndef PATH_TO_APTGET
#	define PATH_TO_APTGET "/usr/bin/apt-get"
#endif /* PATH_TO_APTGET */
/* String found at the beginning of the apt output lines we're interested in */
#define PKGINST_PREFIX "Inst "
/* the RE that catches security updates */
#define SECURITY_RE "^[^\\(]*\\(.* (Debian-Security:|Ubuntu:[^/]*/[^-]*-security)"

/* some standard functions */
typedef struct {
	int errorcode;
	check_apt_config config;
} check_apt_config_wrapper;
static check_apt_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

/* construct the appropriate apt-get cmdline */
static char *construct_cmdline(upgrade_type /*u*/, const char * /*opts*/);
/* run an apt-get update */
static int run_update(char * /*update_opts*/);

typedef struct {
	int errorcode;
	int package_count;
	int security_package_count;
	char **packages_list;
	char **secpackages_list;
} run_upgrade_result;

/* run an apt-get upgrade */
run_upgrade_result run_upgrade(upgrade_type upgrade, const char *do_include, const char *do_exclude, const char *do_critical,
							   const char *upgrade_opts, const char *input_filename);

/* add another clause to a regexp */
static char *add_to_regexp(char * /*expr*/, const char * /*next*/);
/* extract package name from Inst line */
static char *pkg_name(char * /*line*/);
/* string comparison function for qsort */
static int cmpstringp(const void * /*p1*/, const void * /*p2*/);

/* configuration variables */
static int verbose = 0; /* -v */

/* other global variables */
static bool stderr_warning = false; /* if a cmd issued output on stderr */
static bool exec_warning = false;   /* if a cmd exited non-zero */

int main(int argc, char **argv) {
	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_apt_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage_va(_("Could not parse arguments"));
	}

	const check_apt_config config = tmp_config.config;

	/* Set signal handling and alarm timeout */
	if (signal(SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully... */
	alarm(timeout_interval);

	mp_state_enum result = STATE_UNKNOWN;
	/* if they want to run apt-get update first... */
	if (config.do_update) {
		result = run_update(config.update_opts);
	}

	/* apt-get upgrade */
	run_upgrade_result upgrad_res =
		run_upgrade(config.upgrade, config.do_include, config.do_exclude, config.do_critical, config.upgrade_opts, config.input_filename);

	result = max_state(result, upgrad_res.errorcode);
	int packages_available = upgrad_res.package_count;
	int sec_count = upgrad_res.security_package_count;
	char **packages_list = upgrad_res.packages_list;
	char **secpackages_list = upgrad_res.secpackages_list;

	if (sec_count > 0) {
		result = max_state(result, STATE_CRITICAL);
	} else if (packages_available >= config.packages_warning && !config.only_critical) {
		result = max_state(result, STATE_WARNING);
	} else if (result > STATE_UNKNOWN) {
		result = STATE_UNKNOWN;
	}

	printf(_("APT %s: %d packages available for %s (%d critical updates). %s%s%s%s|available_upgrades=%d;;;0 critical_updates=%d;;;0\n"),
		   state_text(result), packages_available, (config.upgrade == DIST_UPGRADE) ? "dist-upgrade" : "upgrade", sec_count,
		   (stderr_warning) ? " warnings detected" : "", (stderr_warning && exec_warning) ? "," : "",
		   (exec_warning) ? " errors detected" : "", (stderr_warning || exec_warning) ? "." : "", packages_available, sec_count);

	if (config.list) {
		qsort(secpackages_list, sec_count, sizeof(char *), cmpstringp);
		qsort(packages_list, packages_available - sec_count, sizeof(char *), cmpstringp);

		for (int i = 0; i < sec_count; i++) {
			printf("%s (security)\n", secpackages_list[i]);
		}

		if (!config.only_critical) {
			for (int i = 0; i < packages_available - sec_count; i++) {
				printf("%s\n", packages_list[i]);
			}
		}
	}

	return result;
}

/* process command-line arguments */
check_apt_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {{"version", no_argument, 0, 'V'},
									   {"help", no_argument, 0, 'h'},
									   {"verbose", no_argument, 0, 'v'},
									   {"timeout", required_argument, 0, 't'},
									   {"update", optional_argument, 0, 'u'},
									   {"upgrade", optional_argument, 0, 'U'},
									   {"no-upgrade", no_argument, 0, 'n'},
									   {"dist-upgrade", optional_argument, 0, 'd'},
									   {"list", no_argument, 0, 'l'},
									   {"include", required_argument, 0, 'i'},
									   {"exclude", required_argument, 0, 'e'},
									   {"critical", required_argument, 0, 'c'},
									   {"only-critical", no_argument, 0, 'o'},
									   {"input-file", required_argument, 0, INPUT_FILE_OPT},
									   {"packages-warning", required_argument, 0, 'w'},
									   {0, 0, 0, 0}};

	check_apt_config_wrapper result = {
		.errorcode = OK,
		.config = check_apt_config_init(),
	};

	while (true) {
		int option_char = getopt_long(argc, argv, "hVvt:u::U::d::nli:e:c:ow:", longopts, NULL);

		if (option_char == -1 || option_char == EOF || option_char == 1) {
			break;
		}

		switch (option_char) {
		case 'h':
			print_help();
			exit(STATE_UNKNOWN);
		case 'V':
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v':
			verbose++;
			break;
		case 't':
			timeout_interval = atoi(optarg);
			break;
		case 'd':
			result.config.upgrade = DIST_UPGRADE;
			if (optarg != NULL) {
				result.config.upgrade_opts = strdup(optarg);
				if (result.config.upgrade_opts == NULL) {
					die(STATE_UNKNOWN, "strdup failed");
				}
			}
			break;
		case 'U':
			result.config.upgrade = UPGRADE;
			if (optarg != NULL) {
				result.config.upgrade_opts = strdup(optarg);
				if (result.config.upgrade_opts == NULL) {
					die(STATE_UNKNOWN, "strdup failed");
				}
			}
			break;
		case 'n':
			result.config.upgrade = NO_UPGRADE;
			break;
		case 'u':
			result.config.do_update = true;
			if (optarg != NULL) {
				result.config.update_opts = strdup(optarg);
				if (result.config.update_opts == NULL) {
					die(STATE_UNKNOWN, "strdup failed");
				}
			}
			break;
		case 'l':
			result.config.list = true;
			break;
		case 'i':
			result.config.do_include = add_to_regexp(result.config.do_include, optarg);
			break;
		case 'e':
			result.config.do_exclude = add_to_regexp(result.config.do_exclude, optarg);
			break;
		case 'c':
			result.config.do_critical = add_to_regexp(result.config.do_critical, optarg);
			break;
		case 'o':
			result.config.only_critical = true;
			break;
		case INPUT_FILE_OPT:
			result.config.input_filename = optarg;
			break;
		case 'w':
			result.config.packages_warning = atoi(optarg);
			break;
		default:
			/* print short usage statement if args not parsable */
			usage5();
		}
	}

	return result;
}

/* run an apt-get upgrade */
run_upgrade_result run_upgrade(const upgrade_type upgrade, const char *do_include, const char *do_exclude, const char *do_critical,
							   const char *upgrade_opts, const char *input_filename) {
	regex_t ereg;
	/* initialize ereg as it is possible it is printed while uninitialized */
	memset(&ereg, '\0', sizeof(ereg.buffer));

	run_upgrade_result result = {
		.errorcode = STATE_UNKNOWN,
	};

	if (upgrade == NO_UPGRADE) {
		result.errorcode = STATE_OK;
		return result;
	}

	int regres = 0;
	regex_t ireg;
	char rerrbuf[64];
	/* compile the regexps */
	if (do_include != NULL) {
		regres = regcomp(&ireg, do_include, REG_EXTENDED);
		if (regres != 0) {
			regerror(regres, &ireg, rerrbuf, 64);
			die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"), progname, rerrbuf);
		}
	}

	if (do_exclude != NULL) {
		regres = regcomp(&ereg, do_exclude, REG_EXTENDED);
		if (regres != 0) {
			regerror(regres, &ereg, rerrbuf, 64);
			die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"), progname, rerrbuf);
		}
	}

	regex_t sreg;
	const char *crit_ptr = (do_critical != NULL) ? do_critical : SECURITY_RE;
	regres = regcomp(&sreg, crit_ptr, REG_EXTENDED);
	if (regres != 0) {
		regerror(regres, &ereg, rerrbuf, 64);
		die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"), progname, rerrbuf);
	}

	struct output chld_out;
	struct output chld_err;
	char *cmdline = NULL;
	cmdline = construct_cmdline(upgrade, upgrade_opts);
	if (input_filename != NULL) {
		/* read input from a file for testing */
		result.errorcode = cmd_file_read(input_filename, &chld_out, 0);
	} else {
		/* run the upgrade */
		result.errorcode = np_runcmd(cmdline, &chld_out, &chld_err, 0);
	}

	/* apt-get upgrade only changes exit status if there is an
	 * internal error when run in dry-run mode.  therefore we will
	 * treat such an error as UNKNOWN */
	if (result.errorcode != STATE_OK) {
		exec_warning = 1;
		result.errorcode = STATE_UNKNOWN;
		fprintf(stderr, _("'%s' exited with non-zero status.\n"), cmdline);
	}

	char **pkglist = malloc(sizeof(char *) * chld_out.lines);
	if (!pkglist) {
		die(STATE_UNKNOWN, "malloc failed!\n");
	}
	char **secpkglist = malloc(sizeof(char *) * chld_out.lines);
	if (!secpkglist) {
		die(STATE_UNKNOWN, "malloc failed!\n");
	}

	/* parse the output, which should only consist of lines like
	 *
	 * Inst package ....
	 * Conf package ....
	 *
	 * so we'll filter based on "Inst" for the time being.  later
	 * we may need to switch to the --print-uris output format,
	 * in which case the logic here will slightly change.
	 */
	int package_counter = 0;
	int security_package_counter = 0;
	for (size_t i = 0; i < chld_out.lines; i++) {
		if (verbose) {
			printf("%s\n", chld_out.line[i]);
		}
		/* if it is a package we care about */
		if (strncmp(PKGINST_PREFIX, chld_out.line[i], strlen(PKGINST_PREFIX)) == 0 &&
			(do_include == NULL || regexec(&ireg, chld_out.line[i], 0, NULL, 0) == 0)) {
			/* if we're not excluding, or it's not in the
			 * list of stuff to exclude */
			if (do_exclude == NULL || regexec(&ereg, chld_out.line[i], 0, NULL, 0) != 0) {
				package_counter++;
				if (regexec(&sreg, chld_out.line[i], 0, NULL, 0) == 0) {
					security_package_counter++;
					if (verbose) {
						printf("*");
					}
					(secpkglist)[security_package_counter - 1] = pkg_name(chld_out.line[i]);
				} else {
					(pkglist)[package_counter - security_package_counter - 1] = pkg_name(chld_out.line[i]);
				}
				if (verbose) {
					printf("*%s\n", chld_out.line[i]);
				}
			}
		}
	}
	result.package_count = package_counter;
	result.security_package_count = security_package_counter;
	result.packages_list = pkglist;
	result.secpackages_list = secpkglist;

	/* If we get anything on stderr, at least set warning */
	if (input_filename == NULL && chld_err.buflen) {
		stderr_warning = true;
		result.errorcode = max_state(result.errorcode, STATE_WARNING);
		if (verbose) {
			for (size_t i = 0; i < chld_err.lines; i++) {
				fprintf(stderr, "%s\n", chld_err.line[i]);
			}
		}
	}
	if (do_include != NULL) {
		regfree(&ireg);
	}
	regfree(&sreg);
	if (do_exclude != NULL) {
		regfree(&ereg);
	}
	free(cmdline);
	return result;
}

/* run an apt-get update (needs root) */
int run_update(char *update_opts) {
	int result = STATE_UNKNOWN;
	char *cmdline;
	/* run the update */
	cmdline = construct_cmdline(NO_UPGRADE, update_opts);

	struct output chld_out;
	struct output chld_err;
	result = np_runcmd(cmdline, &chld_out, &chld_err, 0);
	/* apt-get update changes exit status if it can't fetch packages.
	 * since we were explicitly asked to do so, this is treated as
	 * a critical error. */
	if (result != 0) {
		exec_warning = true;
		result = STATE_CRITICAL;
		fprintf(stderr, _("'%s' exited with non-zero status.\n"), cmdline);
	}

	if (verbose) {
		for (size_t i = 0; i < chld_out.lines; i++) {
			printf("%s\n", chld_out.line[i]);
		}
	}

	/* If we get anything on stderr, at least set warning */
	if (chld_err.buflen) {
		stderr_warning = 1;
		result = max_state(result, STATE_WARNING);
		if (verbose) {
			for (size_t i = 0; i < chld_err.lines; i++) {
				fprintf(stderr, "%s\n", chld_err.line[i]);
			}
		}
	}
	free(cmdline);
	return result;
}

char *pkg_name(char *line) {
	char *start = line + strlen(PKGINST_PREFIX);

	size_t len = strlen(start);

	char *space = index(start, ' ');
	if (space != NULL) {
		len = space - start;
	}

	char *pkg = malloc(sizeof(char) * (len + 1));
	if (!pkg) {
		die(STATE_UNKNOWN, "malloc failed!\n");
	}

	strncpy(pkg, start, len);
	pkg[len] = '\0';

	return pkg;
}

int cmpstringp(const void *left_string, const void *right_string) {
	return strcmp(*(char *const *)left_string, *(char *const *)right_string);
}

char *add_to_regexp(char *expr, const char *next) {
	char *regex_string = NULL;

	if (expr == NULL) {
		regex_string = malloc(sizeof(char) * (strlen("()") + strlen(next) + 1));
		if (!regex_string) {
			die(STATE_UNKNOWN, "malloc failed!\n");
		}
		sprintf(regex_string, "(%s)", next);
	} else {
		/* resize it, adding an extra char for the new '|' separator */
		regex_string = realloc(expr, sizeof(char) * (strlen(expr) + 1 + strlen(next) + 1));
		if (!regex_string) {
			die(STATE_UNKNOWN, "realloc failed!\n");
		}
		/* append it starting at ')' in the old re */
		sprintf((char *)(regex_string + strlen(regex_string) - 1), "|%s)", next);
	}

	return regex_string;
}

char *construct_cmdline(upgrade_type upgrade, const char *opts) {
	const char *opts_ptr = NULL;
	const char *aptcmd = NULL;

	switch (upgrade) {
	case UPGRADE:
		if (opts == NULL) {
			opts_ptr = UPGRADE_DEFAULT_OPTS;
		} else {
			opts_ptr = opts;
		}
		aptcmd = "upgrade";
		break;
	case DIST_UPGRADE:
		if (opts == NULL) {
			opts_ptr = UPGRADE_DEFAULT_OPTS;
		} else {
			opts_ptr = opts;
		}
		aptcmd = "dist-upgrade";
		break;
	case NO_UPGRADE:
		if (opts == NULL) {
			opts_ptr = UPDATE_DEFAULT_OPTS;
		} else {
			opts_ptr = opts;
		}
		aptcmd = "update";
		break;
	}

	int len = 0;
	len += strlen(PATH_TO_APTGET) + 1; /* "/usr/bin/apt-get " */
	len += strlen(opts_ptr) + 1;       /* "opts " */
	len += strlen(aptcmd) + 1;         /* "upgrade\0" */

	char *cmd = (char *)malloc(sizeof(char) * len);
	if (cmd == NULL) {
		die(STATE_UNKNOWN, "malloc failed");
	}
	sprintf(cmd, "%s %s %s", PATH_TO_APTGET, opts_ptr, aptcmd);
	return cmd;
}

/* informative help message */
void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf(_(COPYRIGHT), copyright, email);

	printf("%s\n", _("This plugin checks for software updates on systems that use"));
	printf("%s\n", _("package management systems based on the apt-get(8) command"));
	printf("%s\n", _("found in Debian GNU/Linux"));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_PLUG_TIMEOUT, timeout_interval);

	printf(" %s\n", "-n, --no-upgrade");
	printf("    %s\n", _("Do not run the upgrade.  Probably not useful (without -u at least)."));
	printf(" %s\n", "-l, --list");
	printf("    %s\n", _("List packages available for upgrade.  Packages are printed sorted by"));
	printf("    %s\n", _("name with security packages listed first."));
	printf(" %s\n", "-i, --include=REGEXP");
	printf("    %s\n", _("Include only packages matching REGEXP.  Can be specified multiple times"));
	printf("    %s\n", _("the values will be combined together.  Any packages matching this list"));
	printf("    %s\n", _("cause the plugin to return WARNING status.  Others will be ignored."));
	printf("    %s\n", _("Default is to include all packages."));
	printf(" %s\n", "-e, --exclude=REGEXP");
	printf("    %s\n", _("Exclude packages matching REGEXP from the list of packages that would"));
	printf("    %s\n", _("otherwise be included.  Can be specified multiple times; the values"));
	printf("    %s\n", _("will be combined together.  Default is to exclude no packages."));
	printf(" %s\n", "-c, --critical=REGEXP");
	printf("    %s\n", _("If the full package information of any of the upgradable packages match"));
	printf("    %s\n", _("this REGEXP, the plugin will return CRITICAL status.  Can be specified"));
	printf("    %s\n", _("multiple times like above.  Default is a regexp matching security"));
	printf("    %s\n", _("upgrades for Debian and Ubuntu:"));
	printf("    \t%s\n", SECURITY_RE);
	printf("    %s\n", _("Note that the package must first match the include list before its"));
	printf("    %s\n", _("information is compared against the critical list."));
	printf(" %s\n", "-o, --only-critical");
	printf("    %s\n", _("Only warn about upgrades matching the critical list.  The total number"));
	printf("    %s\n", _("of upgrades will be printed, but any non-critical upgrades will not cause"));
	printf("    %s\n", _("the plugin to return WARNING status."));
	printf(" %s\n", "-w, --packages-warning");
	printf("    %s\n", _("Minimum number of packages available for upgrade to return WARNING status."));
	printf("    %s\n\n", _("Default is 1 package."));

	printf("%s\n\n", _("The following options require root privileges and should be used with care:"));
	printf(" %s\n", "-u, --update=OPTS");
	printf("    %s\n", _("First perform an 'apt-get update'.  An optional OPTS parameter overrides"));
	printf("    %s\n", _("the default options.  Note: you may also need to adjust the global"));
	printf("    %s\n", _("timeout (with -t) to prevent the plugin from timing out if apt-get"));
	printf("    %s\n", _("upgrade is expected to take longer than the default timeout."));
	printf(" %s\n", "-U, --upgrade=OPTS");
	printf("    %s\n", _("Perform an upgrade. If an optional OPTS argument is provided,"));
	printf("    %s\n", _("apt-get will be run with these command line options instead of the"));
	printf("    %s", _("default "));
	printf("(%s).\n", UPGRADE_DEFAULT_OPTS);
	printf("    %s\n", _("Note that you may be required to have root privileges if you do not use"));
	printf("    %s\n", _("the default options, which will only run a simulation and NOT perform the upgrade"));
	printf(" %s\n", "-d, --dist-upgrade=OPTS");
	printf("    %s\n", _("Perform a dist-upgrade instead of normal upgrade. Like with -U OPTS"));
	printf("    %s\n", _("can be provided to override the default options."));

	printf(UT_SUPPORT);
}

/* simple usage heading */
void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s [[-d|-u|-U]opts] [-n] [-l] [-t timeout] [-w packages-warning]\n", progname);
}
