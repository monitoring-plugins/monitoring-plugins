/*****************************************************************************
*
* Monitoring check_curl plugin
*
* License: GPL
* Copyright (c) 1999-2017 Monitoring Plugins Development Team
*
* Description:
*
* This file contains the check_curl plugin
*
* This plugin tests the HTTP service on the specified host. It can test
* normal (http) and secure (https) servers, follow redirects, search for
* strings and regular expressions, check connection times, and report on
* certificate expiration times.
* 
* This plugin uses functions from the curl library, see
* http://curl.haxx.se
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

const char *progname = "check_curl";
const char *copyright = "2006-2017";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"

#ifndef LIBCURL_PROTOCOL_HTTP
#error libcurl compiled without HTTP support, compiling check_curl plugin makes not much sense
#endif

#include "curl/curl.h"
#include "curl/easy.h"

int verbose = FALSE;
CURL *curl;

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);
void print_curl_version (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));
	
	if (curl_global_init (CURL_GLOBAL_DEFAULT) != CURLE_OK)
		die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_global_init failed\n");
		
	if ((curl = curl_easy_init()) == NULL)
		die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_easy_init failed\n");

	curl_easy_cleanup (curl);
	curl_global_cleanup ();
		
	return result;
}

int
process_arguments (int argc, char **argv)
{
	int c;
	int option=0;
	static struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "Vhv", longopts, &option);
		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'h':
			print_help();
			exit(STATE_UNKNOWN);
			break;
		case 'V':
			print_revision(progname, NP_VERSION);
			print_curl_version();
			exit(STATE_UNKNOWN);
			break;
		case 'v':
			verbose++;
			break;
		case '?':
			/* print short usage statement if args not parsable */
			usage5 ();
			break;
		}
	}

	return 0;
}

void
print_help (void)
{
	print_revision(progname, NP_VERSION);

	printf ("Copyright (c) 2017 Andreas Baumann <abaumann@yahoo.com>\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin tests the HTTP(S) service on the specified host."));
	printf ("%s\n", _("It makes use of libcurl to do so."));

	printf ("\n\n");

	print_usage();
	printf (_("NOTE: One or both of -H and -I must be specified"));

	printf ("\n");

	printf (UT_HELP_VRSN);
	printf (UT_VERBOSE);

	printf (UT_SUPPORT);

	printf ("%s\n", _("WARNING: check_curl is experimental. Please use"));
	printf ("%s\n\n", _("check_http if you need a stable version."));
}

void
print_usage (void)
{
	printf ("%s\n", _("WARNING: check_curl is experimental. Please use"));
	printf ("%s\n\n", _("check_http if you need a stable version."));
	printf ("%s\n", _("Usage:"));
	printf (" %s [-v verbose]\n", progname);
}

void
print_curl_version (void)
{
	printf( "%s\n", curl_version());
}
