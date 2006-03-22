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

#define APTGET_UPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq upgrade"
#define APTGET_DISTUPGRADE "/usr/bin/apt-get -o 'Debug::NoLocking=true' -s -qq dist-upgrade"
#define APTGET_UPDATE "/usr/bin/apt-get update"

int process_arguments(int, char **);
void print_help(void);
void print_usage(void);

int run_upgrade(int *pkgcount);

static int verbose = 0;

int main (int argc, char **argv) {
	int result=STATE_UNKNOWN, packages_available=0;

	if (process_arguments(argc, argv) == ERROR)
		usage_va(_("Could not parse arguments"));

	/* Set signal handling and alarm timeout */
	if (signal (SIGALRM, timeout_alarm_handler) == SIG_ERR) {
		usage_va(_("Cannot catch SIGALRM"));
	}

	/* handle timeouts gracefully... */
	alarm (timeout_interval);

	/* apt-get upgrade */
	result = run_upgrade(&packages_available);

	if(packages_available > 0){
		result = STATE_WARNING;
		printf("APT WARNING: ");
	} else {
		result = STATE_OK;
		printf("APT OK: ");
	}
	printf("%d packages available for upgrade\n", packages_available);

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
		{0, 0, 0, 0}
	};

	while(1) {
		c = getopt_long(argc, argv, "hVvt", longopts, NULL);

		if(c == -1 || c == EOF || c == 1) break;

		switch(c) {
		case 'h':									/* help */
			print_help();
			exit(STATE_OK);
		case 'V':									/* version */
			print_revision(progname, revision);
			exit(STATE_OK);
		case 'v':
			verbose++;
			break;
		case 't':
			timeout_interval=atoi(optarg);
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
	printf(_("\
 -t, --timeout=INTEGER\n\
   Seconds to wait for plugin execution to complete\n\
"));
}

/* simple usage heading */
void print_usage(void){
	printf ("Usage: %s [-u] [-t timeout]\n", progname);
}

/* run an apt-get upgrade */
int run_upgrade(int *pkgcount){
	int i=0, result=STATE_UNKNOWN, pc=0;
	struct output chld_out, chld_err;

	/* run the upgrade */
	if((result = np_runcmd(APTGET_UPGRADE, &chld_out, &chld_err, 0)) != 0)
		result = STATE_WARNING;

	/* parse the output, which should only consist of lines like
	 *
	 * Inst package ....
	 * Conf package ....
	 *
	 * so we'll filter based on "Inst"
	 */
	for(i = 0; i < chld_out.lines; i++) {
		if(strncmp(chld_out.line[i], "Inst", 4)==0){
			if(verbose){
				printf("%s\n", chld_out.line[i]);
			}
			pc++;
		}
	}
	*pkgcount=pc;

	/* If we get anything on stderr, at least set warning */
	if(chld_err.buflen){
		fprintf(stderr, "warning, output detected on stderr\n");
		for(i = 0; i < chld_err.lines; i++) {
			printf("got this: %s\n", chld_err.line[i]);
			result = max_state (result, STATE_WARNING);
		}
	}

	return result;
}
