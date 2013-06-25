/*****************************************************************************
* 
* Nagios check_apt plugin
* 
* License: GPL
* Copyright (c) 2006-2008 Nagios Plugins Development Team
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

const char *progname = "check_apt";
const char *copyright = "2006-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "runcmd.h"
#include "utils.h"
#include "regex.h"

/* some constants */
typedef enum { UPGRADE, DIST_UPGRADE, NO_UPGRADE } upgrade_type;

/* Character for hidden input file option (for testing). */
#define INPUT_FILE_OPT CHAR_MAX+1
/* the default opts can be overridden via the cmdline */
#define UPGRADE_DEFAULT_OPTS "-o 'Debug::NoLocking=true' -s -qq"
#define UPDATE_DEFAULT_OPTS "-q"
/* until i commit the configure.in patch which gets this, i'll define
 * it here as well */
#ifndef PATH_TO_APTGET
# define PATH_TO_APTGET "/usr/bin/apt-get"
#endif /* PATH_TO_APTGET */
/* String found at the beginning of the apt output lines we're interested in */
#define PKGINST_PREFIX "Inst "
/* the RE that catches security updates */
#define SECURITY_RE "^[^\\(]*\\(.* (Debian-Security:|Ubuntu:[^/]*/[^-]*-security)"

/* some standard functions */
int process_arguments(int, char **);
void print_help(void);
void print_usage(void);

/* construct the appropriate apt-get cmdline */
char* construct_cmdline(upgrade_type u, const char *opts);
/* run an apt-get update */
int run_update(void);
/* run an apt-get upgrade */
int run_upgrade(int *pkgcount, int *secpkgcount);
/* add another clause to a regexp */
char* add_to_regexp(char *expr, const char *next);

/* configuration variables */
static int verbose = 0;      /* -v */
static int do_update = 0;    /* whether to call apt-get update */
static upgrade_type upgrade = UPGRADE; /* which type of upgrade to do */
static char *upgrade_opts = NULL; /* options to override defaults for upgrade */
static char *update_opts = NULL; /* options to override defaults for update */
static char *do_include = NULL;  /* regexp to only include certain packages */
static char *do_exclude = NULL;  /* regexp to only exclude certain packages */
static char *do_critical = NULL;  /* regexp specifying critical packages */
static char *input_filename = NULL; /* input filename for testing */

/* other global variables */
static int stderr_warning = 0;   /* if a cmd issued output on stderr */
static int exec_warning = 0;     /* if a cmd exited non-zero */

int main (int argc, char **argv) {
	int result=STATE_UNKNOWN, packages_available=0, sec_count=0;

	/* Parse extra opts if any */
	argv=np_extra_opts(&argc, argv, progname);

	if (process_arguments(argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* if they want to run apt-get update first... */
	if(do_update) result = run_update();

	/* apt-get upgrade */
	result = max_state(result, run_upgrade(&packages_available, &sec_count));

	if(sec_count > 0){
		result = max_state(result, STATE_CRITICAL);
	} else if(packages_available > 0){
		result = max_state(result, STATE_WARNING);
	} else if(result > STATE_UNKNOWN){
		result = STATE_UNKNOWN;
	}

	printf(_("APT %s: %d packages available for %s (%d critical updates). %s%s%s%s|available_upgrades=%d;;;0 critical_updates=%d;;;0\n"),
	       state_text(result),
	       packages_available,
	       (upgrade==DIST_UPGRADE)?"dist-upgrade":"upgrade",
		   sec_count,
	       (stderr_warning)?" warnings detected":"",
	       (stderr_warning && exec_warning)?",":"",
	       (exec_warning)?" errors detected":"",
	       (stderr_warning||exec_warning)?". run with -v for information.":"",
	        packages_available,
		   sec_count
	       );

	return result;
}

/* process command-line arguments */
int process_arguments (int argc, char **argv) {
	int c;

	static struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"timeout", required_argument, 0, 't'},
		{"update", optional_argument, 0, 'u'},
		{"upgrade", optional_argument, 0, 'U'},
		{"no-upgrade", no_argument, 0, 'n'},
		{"dist-upgrade", optional_argument, 0, 'd'},
		{"include", required_argument, 0, 'i'},
		{"exclude", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"input-file", required_argument, 0, INPUT_FILE_OPT},
		{0, 0, 0, 0}
	};

	while(1) {
		c = getopt_long(argc, argv, "hVvt:u::U::d::ni:e:c:", longopts, NULL);

		if(c == -1 || c == EOF || c == 1) break;

		switch(c) {
		case 'h':
			print_help();
			exit(STATE_OK);
		case 'V':
			print_revision(progname, NP_VERSION);
			exit(STATE_OK);
		case 'v':
			verbose++;
			break;
		case 't':
			timeout_interval=atoi(optarg);
			break;
		case 'd':
			upgrade=DIST_UPGRADE;
			if(optarg!=NULL){
				upgrade_opts=strdup(optarg);
				if(upgrade_opts==NULL) die(STATE_UNKNOWN, "strdup failed");
			}
			break;
		case 'U':
			upgrade=UPGRADE;
			if(optarg!=NULL){
				upgrade_opts=strdup(optarg);
				if(upgrade_opts==NULL) die(STATE_UNKNOWN, "strdup failed");
			}
			break;
		case 'n':
			upgrade=NO_UPGRADE;
			break;
		case 'u':
			do_update=1;
			if(optarg!=NULL){
				update_opts=strdup(optarg);
				if(update_opts==NULL) die(STATE_UNKNOWN, "strdup failed");
			}
			break;
		case 'i':
			do_include=add_to_regexp(do_include, optarg);
			break;
		case 'e':
			do_exclude=add_to_regexp(do_exclude, optarg);
			break;
		case 'c':
			do_critical=add_to_regexp(do_critical, optarg);
			break;
		case INPUT_FILE_OPT:
			input_filename = optarg;
			break;
		default:
			/* print short usage statement if args not parsable */
			usage5();
		}
	}

	return OK;
}


/* run an apt-get upgrade */
int run_upgrade(int *pkgcount, int *secpkgcount){
	int i=0, result=STATE_UNKNOWN, regres=0, pc=0, spc=0;
	struct output chld_out, chld_err;
	regex_t ireg, ereg, sreg;
	char *cmdline=NULL, rerrbuf[64];

	if(upgrade==NO_UPGRADE) return STATE_OK;

	/* compile the regexps */
	if (do_include != NULL) {
		regres=regcomp(&ireg, do_include, REG_EXTENDED);
		if (regres!=0) {
			regerror(regres, &ireg, rerrbuf, 64);
			die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"), progname, rerrbuf);
		}
	}
   
	if(do_exclude!=NULL){
		regres=regcomp(&ereg, do_exclude, REG_EXTENDED);
		if(regres!=0) {
			regerror(regres, &ereg, rerrbuf, 64);
			die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"),
			    progname, rerrbuf);
		}
	}
   
	const char *crit_ptr = (do_critical != NULL) ? do_critical : SECURITY_RE;
	regres=regcomp(&sreg, crit_ptr, REG_EXTENDED);
	if(regres!=0) {
		regerror(regres, &ereg, rerrbuf, 64);
		die(STATE_UNKNOWN, _("%s: Error compiling regexp: %s"),
		    progname, rerrbuf);
	}

	cmdline=construct_cmdline(upgrade, upgrade_opts);
	if (input_filename != NULL) {
		/* read input from a file for testing */
		result = cmd_file_read(input_filename, &chld_out, 0);
	} else {
		/* run the upgrade */
		result = np_runcmd(cmdline, &chld_out, &chld_err, 0);
	}
   
	/* apt-get upgrade only changes exit status if there is an
	 * internal error when run in dry-run mode.  therefore we will
	 * treat such an error as UNKNOWN */
	if(result != 0){
		exec_warning=1;
		result = STATE_UNKNOWN;
		fprintf(stderr, _("'%s' exited with non-zero status.\n"),
		    cmdline);
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
	for(i = 0; i < chld_out.lines; i++) {
		if(verbose){
			printf("%s\n", chld_out.line[i]);
		}
		/* if it is a package we care about */
		if (strncmp(PKGINST_PREFIX, chld_out.line[i], strlen(PKGINST_PREFIX)) == 0 &&
		    (do_include == NULL || regexec(&ireg, chld_out.line[i], 0, NULL, 0) == 0)) {
			/* if we're not excluding, or it's not in the
			 * list of stuff to exclude */
			if(do_exclude==NULL ||
			   regexec(&ereg, chld_out.line[i], 0, NULL, 0)!=0){
				pc++;
				if(regexec(&sreg, chld_out.line[i], 0, NULL, 0)==0){
					spc++;
					if(verbose) printf("*");
				}
				if(verbose){
					printf("*%s\n", chld_out.line[i]);
				}
			}
		}
	}
	*pkgcount=pc;
	*secpkgcount=spc;

	/* If we get anything on stderr, at least set warning */
	if (input_filename == NULL && chld_err.buflen) {
		stderr_warning=1;
		result = max_state(result, STATE_WARNING);
		if(verbose){
			for(i = 0; i < chld_err.lines; i++) {
				fprintf(stderr, "%s\n", chld_err.line[i]);
			}
		}
	}
	if (do_include != NULL) regfree(&ireg);
	regfree(&sreg);
	if(do_exclude!=NULL) regfree(&ereg);
	free(cmdline);
	return result;
}

/* run an apt-get update (needs root) */
int run_update(void){
	int i=0, result=STATE_UNKNOWN;
	struct output chld_out, chld_err;
	char *cmdline;

	/* run the upgrade */
	cmdline = construct_cmdline(NO_UPGRADE, update_opts);
	result = np_runcmd(cmdline, &chld_out, &chld_err, 0);
	/* apt-get update changes exit status if it can't fetch packages.
	 * since we were explicitly asked to do so, this is treated as
	 * a critical error. */
	if(result != 0){
		exec_warning=1;
		result = STATE_CRITICAL;
		fprintf(stderr, _("'%s' exited with non-zero status.\n"),
		        cmdline);
	}

	if(verbose){
		for(i = 0; i < chld_out.lines; i++) {
			printf("%s\n", chld_out.line[i]);
		}
	}

	/* If we get anything on stderr, at least set warning */
	if(chld_err.buflen){
		stderr_warning=1;
		result = max_state(result, STATE_WARNING);
		if(verbose){
			for(i = 0; i < chld_err.lines; i++) {
				fprintf(stderr, "%s\n", chld_err.line[i]);
			}
		}
	}
	free(cmdline);
	return result;
}

char* add_to_regexp(char *expr, const char *next){
	char *re=NULL;

	if(expr==NULL){
		re=malloc(sizeof(char)*(strlen("()")+strlen(next)+1));
		if(!re) die(STATE_UNKNOWN, "malloc failed!\n");
		sprintf(re, "(%s)", next);
	} else {
		/* resize it, adding an extra char for the new '|' separator */
		re=realloc(expr, sizeof(char)*(strlen(expr)+1+strlen(next)+1));
		if(!re) die(STATE_UNKNOWN, "realloc failed!\n");
		/* append it starting at ')' in the old re */
		sprintf((char*)(re+strlen(re)-1), "|%s)", next);
	}

	return re;
}

char* construct_cmdline(upgrade_type u, const char *opts){
	int len=0;
	const char *opts_ptr=NULL, *aptcmd=NULL;
	char *cmd=NULL;

	switch(u){
	case UPGRADE:
		if(opts==NULL) opts_ptr=UPGRADE_DEFAULT_OPTS;
		else opts_ptr=opts;
		aptcmd="upgrade";
		break;
	case DIST_UPGRADE:
		if(opts==NULL) opts_ptr=UPGRADE_DEFAULT_OPTS;
		else opts_ptr=opts;
		aptcmd="dist-upgrade";
		break;
	case NO_UPGRADE:
		if(opts==NULL) opts_ptr=UPDATE_DEFAULT_OPTS;
		else opts_ptr=opts;
		aptcmd="update";
		break;
	}

	len+=strlen(PATH_TO_APTGET)+1; /* "/usr/bin/apt-get " */
	len+=strlen(opts_ptr)+1;       /* "opts " */
	len+=strlen(aptcmd)+1;         /* "upgrade\0" */

	cmd=(char*)malloc(sizeof(char)*len);
	if(cmd==NULL) die(STATE_UNKNOWN, "malloc failed");
	sprintf(cmd, "%s %s %s", PATH_TO_APTGET, opts_ptr, aptcmd);
	return cmd;
}

/* informative help message */
void
print_help (void)
{
  print_revision(progname, NP_VERSION);

  printf(_(COPYRIGHT), copyright, email);

  printf("%s\n", _("This plugin checks for software updates on systems that use"));
  printf("%s\n", _("package management systems based on the apt-get(8) command"));
  printf("%s\n", _("found in Debian GNU/Linux"));

  printf ("\n\n");

  print_usage();

  printf(UT_HELP_VRSN);
  printf(UT_EXTRA_OPTS);

  printf(UT_TIMEOUT, timeout_interval);

  printf (" %s\n", "-U, --upgrade=OPTS");
  printf ("    %s\n", _("[Default] Perform an upgrade.  If an optional OPTS argument is provided,"));
  printf ("    %s\n", _("apt-get will be run with these command line options instead of the"));
  printf ("    %s", _("default "));
  printf ("(%s).\n", UPGRADE_DEFAULT_OPTS);
  printf ("    %s\n", _("Note that you may be required to have root privileges if you do not use"));
  printf ("    %s\n", _("the default options."));
  printf (" %s\n", "-d, --dist-upgrade=OPTS");
  printf ("    %s\n", _("Perform a dist-upgrade instead of normal upgrade. Like with -U OPTS"));
  printf ("    %s\n", _("can be provided to override the default options."));
  printf (" %s\n", " -n, --no-upgrade");
  printf ("    %s\n", _("Do not run the upgrade.  Probably not useful (without -u at least)."));
  printf (" %s\n", "-i, --include=REGEXP");
  printf ("    %s\n", _("Include only packages matching REGEXP.  Can be specified multiple times"));
  printf ("    %s\n", _("the values will be combined together.  Any packages matching this list"));
  printf ("    %s\n", _("cause the plugin to return WARNING status.  Others will be ignored."));
  printf ("    %s\n", _("Default is to include all packages."));
  printf (" %s\n", "-e, --exclude=REGEXP");
  printf ("    %s\n", _("Exclude packages matching REGEXP from the list of packages that would"));
  printf ("    %s\n", _("otherwise be included.  Can be specified multiple times; the values"));
  printf ("    %s\n", _("will be combined together.  Default is to exclude no packages."));
  printf (" %s\n", "-c, --critical=REGEXP");
  printf ("    %s\n", _("If the full package information of any of the upgradable packages match"));
  printf ("    %s\n", _("this REGEXP, the plugin will return CRITICAL status.  Can be specified"));
  printf ("    %s\n", _("multiple times like above.  Default is a regexp matching security"));
  printf ("    %s\n", _("upgrades for Debian and Ubuntu:"));
  printf ("    \t\%s\n", SECURITY_RE);
  printf ("    %s\n", _("Note that the package must first match the include list before its"));
  printf ("    %s\n\n", _("information is compared against the critical list."));

  printf ("%s\n\n", _("The following options require root privileges and should be used with care:"));
  printf (" %s\n", "-u, --update=OPTS");
  printf ("    %s\n", _("First perform an 'apt-get update'.  An optional OPTS parameter overrides"));
  printf ("    %s\n", _("the default options.  Note: you may also need to adjust the global"));
  printf ("    %s\n", _("timeout (with -t) to prevent the plugin from timing out if apt-get"));
  printf ("    %s\n", _("upgrade is expected to take longer than the default timeout."));

  printf(UT_SUPPORT);
}


/* simple usage heading */
void
print_usage(void)
{
  printf ("%s\n", _("Usage:"));
  printf ("%s [[-d|-u|-U]opts] [-n] [-t timeout]\n", progname);
}
