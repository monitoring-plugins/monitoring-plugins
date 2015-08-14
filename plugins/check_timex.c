/*****************************************************************************
*
* Monitoring check_timex plugin
*
* License: GPL
* Copyright (c) 2015 Monitoring Plugins Development Team
* Author: Sami Kerola <kerolasa@iki.fi>
*
* Description:
*
* This file contains the check_timex plugin
*
* This plugin uses ntp_gettime() interface to monitor system clock.
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

const char *progname = "check_timex";
const char *copyright = "2015";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"

#include <sys/timex.h>

long int warning_offset;
int check_warning_offset = FALSE;
long int critical_offset;
int check_critical_offset = FALSE;

void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf (_("%s [-w offset_warn] [-c offset_crit]\n"), progname);
}

static void
print_help (void)
{
	print_revision (progname, NP_VERSION);
	printf ("Copyright (c) 2015 Monitoring Plugins Development Team\n");
	printf (COPYRIGHT, copyright, email);
	printf ("%s\n",			_("This plugin will check if the system time is syncronized."));
	printf ("\n\n");
	print_usage ();
	printf (UT_HELP_VRSN);
	printf (" %s\n", "-w, --warning=THRESHOLD");
	printf ("    %s\n",		_("Offset to result in warning status (microseconds)"));
	printf (" %s\n", "-c, --critical=THRESHOLD");
	printf ("    %s\n",		_("Offset to result in critical status (microseconds)"));
	printf ("\n");
	printf ("   %s\n",		_("System clock out of sync will always result critical status."));
	printf (UT_SUPPORT);
}

static int
process_arguments (int argc, char **argv)
{
	static const struct option longopts[] = {
		{"warning", required_argument, NULL, 'w'},
		{"critical", required_argument, NULL, 'c'},
		{"version", no_argument, NULL, 'V'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};
	int c;

	while (1) {
		c = getopt_long (argc, argv, "w:c:Vh", longopts, NULL);
		if (c == -1 || c == EOF)
			break;
		switch (c) {
		case '?':
			usage5 ();
		case 'h':
			print_help ();
			exit (STATE_OK);
		case 'V':
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'w':
			if (is_intnonneg (optarg)) {
				warning_offset = strtol (optarg, NULL, 10);
				check_warning_offset = TRUE;
			}
			else {
				usage4 (_("Warning threshold must be a positive integer"));
			}
			break;
		case 'c':
			if (is_intnonneg (optarg)) {
				critical_offset = strtol (optarg, NULL, 10);
				check_critical_offset = TRUE;
			}
			else {
				usage4 (_("Critical threshold must be a positive integer"));
			}
			break;
		}
	}
	c = optind;
	return OK;
}

static int
with_in_limits (long int threshold, long int value)
{
	if (threshold < labs (value))
		return 1;
	return 0;
}

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN, ret;
	struct timex tx = { 0 };

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);
	if (process_arguments (argc, argv) == ERROR) {
		usage4 (_("Could not parse arguments"));
	}
	if (check_critical_offset && check_warning_offset) {
		if (critical_offset < warning_offset) {
			die (STATE_UNKNOWN,
					 _("Parameter inconsistency: warning is greater than critical\n"));
		}
	}
	ret = ntp_adjtime (&tx);
	if (ret == TIME_ERROR) {
		die (STATE_CRITICAL,
				 "TIMEX CRITICAL: The precision clock model is not properly set up\n");
	}
	fputs ("TIMEX ", stdout);
	if (check_critical_offset && with_in_limits (critical_offset, tx.offset)) {
		result = STATE_CRITICAL;
		fputs ("CRITICAL: ", stdout);
	}
	else if (check_warning_offset && with_in_limits (warning_offset, tx.offset)) {
		result = STATE_WARNING;
		fputs ("WARNING: ", stdout);
	}
	else {
		fputs ("OK: ", stdout);
	}
	printf ("Estimated error %ld|", tx.offset);
	printf ("offset=%ld;%ld;%ld ", tx.offset, critical_offset, warning_offset);
	printf ("maxerror=%ld ", tx.maxerror);
	printf ("esterror=%ld ", tx.esterror);
	printf ("precision=%ld ", tx.precision);
	printf ("jitter=%ld ", tx.jitter);
	printf ("stabil=%ld ", tx.stabil);
	printf ("jitcnt=%ld ", tx.jitcnt);
	printf ("errcnt=%ld ", tx.errcnt);
	printf ("stbcnt=%ld\n", tx.stbcnt);
	return result;
}
