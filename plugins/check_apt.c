/******************************************************************************
 * check_apt.c: check for available updates in apt package management systems
 * original author: sean finney <seanius@seanius.net> 
 *                  (with some common bits stolen from check_nagios.c)
 ******************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
******************************************************************************/

const char *progname = "check_apt";
const char *revision = "$Revision$";
const char *copyright = "2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "runcmd.h"
#include "utils.h"
#include <regex.h>

/* for now define the various apt calls as constants.  this may need
 * to change later. */
#define APTGET_UPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq upgrade"
#define APTGET_DISTUPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq dist-upgrade"
#define APTGET_UPDATE "/usr/bin/apt-get -q update"

#define SECURITY_RE "^[^\\(]*\\([^ ]* (Debian-Security:|Ubuntu:[^/]*/[^-]*-security)"

/* some standard functions */
int process_arguments(int, char **);
void print_help(void);
void print_usage(void);

/* run an apt-get update */
int run_update(void);
/* run an apt-get upgrade */
int run_upgrade(int *pkgcount, int *secpkgcount);
/* add another clause to a regexp */
char* add_to_regexp(char *expr, const char *next);

/* configuration variables */
static int verbose = 0;      /* -v */
static int do_update = 0;    /* whether to call apt-get update */
static int dist_upgrade = 0; /* whether to call apt-get dist-upgrade */
static char* do_include = NULL;  /* regexp to only include certain packages */
static char* do_exclude = NULL;  /* regexp to only exclude certain packages */

/* other global variables */
static int stderr_warning = 0;   /* if a cmd issued output on stderr */
static int exec_warning = 0;     /* if a cmd exited non-zero */

int main (int argc, char **argv) {
	int result=STATE_UNKNOWN, packages_available=0, sec_count=0;

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
	} else {
		result = max_state(result, STATE_OK);
	}

	printf("APT %s: %d packages available for %s (%d critical updates). %s%s%s%s\n", 
	       state_text(result),
	       packages_available,
	       (dist_upgrade)?"dist-upgrade":"upgrade",
		   sec_count,
	       (stderr_warning)?" warnings detected":"",
	       (stderr_warning && exec_warning)?",":"",
	       (exec_warning)?" errors detected":"",
	       (stderr_warning||exec_warning)?". run with -v for information.":""
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
		{"update", no_argument, 0, 'u'},
		{"dist-upgrade", no_argument, 0, 'd'},
		{"include", no_argument, 0, 'i'},
		{"exclude", no_argument, 0, 'e'},
		{0, 0, 0, 0}
	};

	while(1) {
		c = getopt_long(argc, argv, "hVvt:udi:e:", longopts, NULL);

		if(c == -1 || c == EOF || c == 1) break;

		switch(c) {
		case 'h':
			print_help();
			exit(STATE_OK);
		case 'V':
			print_revision(progname, revision);
			exit(STATE_OK);
		case 'v':
			verbose++;
			break;
		case 't':
			timeout_interval=atoi(optarg);
			break;
		case 'd':
			dist_upgrade=1;
			break;
		case 'u':
			do_update=1;
			break;
		case 'i':
			do_include=add_to_regexp(do_include, optarg);
			break;
		case 'e':
			do_exclude=add_to_regexp(do_exclude, optarg);
			break;
		default:
			/* print short usage statement if args not parsable */
			usage_va(_("Unknown argument - %s"), optarg);
		}
	}

	return OK;
}


/* informative help message */
void print_help(void){
	print_revision(progname, revision);
	printf(_(COPYRIGHT), copyright, email);
	printf(_("\
This plugin checks for software updates on systems that use\n\
package management systems based on the apt-get(8) command\n\
found in Debian GNU/Linux\n\
\n\n"));
	print_usage();
	printf(_(UT_HELP_VRSN));
	printf(_(UT_TIMEOUT), timeout_interval);
   	printf(_("\n\
 -d, --dist-upgrade\n\
   Perform a dist-upgrade instead of normal upgrade.\n\
 -i, --include=REGEXP\n\
   Include only packages matching REGEXP.  Can be specified multiple times;\n\
   the values will be combined together.  Default is to include all packages.\n\
 -e, --exclude=REGEXP\n\
   Exclude packages matching REGEXP from the list of packages that would\n\
   otherwise be excluded.  Can be specified multiple times; the values\n\
   will be combined together.  Default is to exclude no packages.\n\n"));
   	printf(_("\
The following options require root privileges and should be used with care: \
\n\n"));
   	printf(_("\
 -u, --update\n\
   First perform an 'apt-get update' (note: you may also need to use -t)\
\n\n"));
}

/* simple usage heading */
void print_usage(void){
	printf ("Usage: %s [-du] [-t timeout]\n", progname);
}

/* run an apt-get upgrade */
int run_upgrade(int *pkgcount, int *secpkgcount){
	int i=0, result=STATE_UNKNOWN, regres=0, pc=0, spc=0;
	struct output chld_out, chld_err;
	regex_t ireg, ereg, sreg;
	char rerrbuf[64];
	const char *default_include_expr="^Inst";

	/* compile the regexps */
	if(do_include!=NULL){
		regres=regcomp(&ireg, do_include, REG_EXTENDED);
		if(regres!=0) {
			regerror(regres, &ireg, rerrbuf, 64);
			die(STATE_UNKNOWN, "%s: Error compiling regexp: %s",
			    progname, rerrbuf);
		}
	} else {
		regres=regcomp(&ireg, default_include_expr, REG_EXTENDED);
		if(regres!=0) {
			regerror(regres, &ireg, rerrbuf, 64);
			die(STATE_UNKNOWN, "%s: Error compiling regexp: %s",
			    progname, rerrbuf);
		}
	}
	if(do_exclude!=NULL){
		regres=regcomp(&ereg, do_exclude, REG_EXTENDED);
		if(regres!=0) {
			regerror(regres, &ereg, rerrbuf, 64);
			die(STATE_UNKNOWN, "%s: Error compiling regexp: %s",
			    progname, rerrbuf);
		}
	}
	regres=regcomp(&sreg, SECURITY_RE, REG_EXTENDED);
	if(regres!=0) {
		regerror(regres, &ereg, rerrbuf, 64);
		die(STATE_UNKNOWN, "%s: Error compiling regexp: %s",
		    progname, rerrbuf);
	}



	/* run the upgrade */
	if(dist_upgrade==0){
		result = np_runcmd(APTGET_UPGRADE, &chld_out, &chld_err, 0);
	} else {
		result = np_runcmd(APTGET_DISTUPGRADE, &chld_out, &chld_err, 0);
	}
	/* apt-get upgrade only changes exit status if there is an
	 * internal error when run in dry-run mode.  therefore we will
	 * treat such an error as UNKNOWN */
	if(result != 0){
		exec_warning=1;
		result = STATE_UNKNOWN;
		fprintf(stderr, "'%s' exited with non-zero status.\n",
		    APTGET_UPGRADE);
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
		if(regexec(&ireg, chld_out.line[i], 0, NULL, 0)==0){
			/* if we're not excluding, or it's not in the
			 * list of stuff to exclude */
			if(do_exclude==NULL ||
			   regexec(&ereg, chld_out.line[i], 0, NULL, 0)!=0){
				pc++;
				if(regexec(&sreg, chld_out.line[i], 0, NULL, 0)==0){
					spc++;
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
	if(chld_err.buflen){
		stderr_warning=1;
		result = max_state(result, STATE_WARNING);
		if(verbose){
			for(i = 0; i < chld_err.lines; i++) {
				fprintf(stderr, "%s\n", chld_err.line[i]);
			}
		}
	}
	return result;
}

/* run an apt-get update (needs root) */
int run_update(void){
	int i=0, result=STATE_UNKNOWN;
	struct output chld_out, chld_err;

	/* run the upgrade */
	result = np_runcmd(APTGET_UPDATE, &chld_out, &chld_err, 0);
	/* apt-get update changes exit status if it can't fetch packages.
	 * since we were explicitly asked to do so, this is treated as
	 * a critical error. */
	if(result != 0){
		exec_warning=1;
		result = STATE_CRITICAL;
		fprintf(stderr, "'%s' exited with non-zero status.\n",
		        APTGET_UPDATE);
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
	return result;
}

char* add_to_regexp(char *expr, const char *next){
	char *re=NULL;

	if(expr==NULL){
		re=malloc(sizeof(char)*(strlen("^Inst () ")+strlen(next)+1));
		if(!re) die(STATE_UNKNOWN, "malloc failed!\n");
		sprintf(re, "^Inst (%s) ", next);
	} else {
		/* resize it, adding an extra char for the new '|' separator */
		re=realloc(expr, sizeof(char)*strlen(expr)+1+strlen(next)+1);
		if(!re) die(STATE_UNKNOWN, "realloc failed!\n");
		/* append it starting at ')' in the old re */
		sprintf((char*)(re+strlen(re)-2), "|%s) ", next);
	}

	return re;	
}
