/******************************************************************************

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

const char *progname = "check_load";
const char *revision = "$Revision$";
const char *copyright = "1999-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "utils.h"
#include "popen.h"

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

/* needed for compilation under NetBSD, as suggested by Andy Doran */
#ifndef LOADAVG_1MIN
#define LOADAVG_1MIN	0
#define LOADAVG_5MIN	1
#define LOADAVG_15MIN	2
#endif /* !defined LOADAVG_1MIN */


int process_arguments (int argc, char **argv);
int validate_arguments (void);
void print_help (void);
void print_usage (void);

float wload1 = -1, wload5 = -1, wload15 = -1;
float cload1 = -1, cload5 = -1, cload15 = -1;

char *status_line;



int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	
#if HAVE_GETLOADAVG==1
	double la[3] = { 0.0, 0.0, 0.0 };	/* NetBSD complains about unitialized arrays */
#else
# if HAVE_PROC_LOADAVG==1
	FILE *fp;
	char input_buffer[MAX_INPUT_BUFFER];
	char *tmp_ptr;
# else
	char input_buffer[MAX_INPUT_BUFFER];
# endif
#endif

	float la1, la5, la15;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) != TRUE)
		usage4 (_("Could not parse arguments"));

#if HAVE_GETLOADAVG==1
	result = getloadavg (la, 3);
	if (result == -1)
		return STATE_UNKNOWN;
	la1 = la[LOADAVG_1MIN];
	la5 = la[LOADAVG_5MIN];
	la15 = la[LOADAVG_15MIN];
#else
# if HAVE_PROC_LOADAVG==1
	fp = fopen (PROC_LOADAVG, "r");
	if (fp == NULL) {
		printf (_("Error opening %s\n"), PROC_LOADAVG);
		return STATE_UNKNOWN;
	}

	la1 = la5 = la15 = -1;

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		tmp_ptr = strtok (input_buffer, " ");
		la1 = atof (tmp_ptr);
		tmp_ptr = strtok (NULL, " ");
		la5 = atof (tmp_ptr);
		tmp_ptr = strtok (NULL, " ");
		la15 = atof (tmp_ptr);
	}

	fclose (fp);
# else
	child_process = spopen (PATH_TO_UPTIME);
	if (child_process == NULL) {
		printf (_("Error opening %s\n"), PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf (_("Could not open stderr for %s\n"), PATH_TO_UPTIME);
	}
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	sscanf (input_buffer, "%*[^l]load average: %f, %f, %f", &la1, &la5, &la15);

	result = spclose (child_process);
	if (result) {
		printf (_("Error code %d returned in %s\n"), result, PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
# endif
#endif


	if ((la1 < 0.0) || (la5 < 0.0) || (la15 < 0.0)) {
#if HAVE_GETLOADAVG==1
		printf (_("Error in getloadavg()\n"));
#else
# if HAVE_PROC_LOADAVG==1
		printf (_("Error processing %s\n"), PROC_LOADAVG);
# else
		printf (_("Error processing %s\n"), PATH_TO_UPTIME);
# endif
#endif
		return STATE_UNKNOWN;
	}

	asprintf(&status_line, _("load average: %.2f, %.2f, %.2f"), la1, la5, la15);

	if ((la1 >= cload1) || (la5 >= cload5) || (la15 >= cload15))
		result = STATE_CRITICAL;
	else if ((la1 >= wload1) || (la5 >= wload5) || (la15 >= wload15))
		result = STATE_WARNING;
	else
		result = STATE_OK;

	die (result,
	     "%s - %s|%s %s %s\n",
	     state_text (result),
	     status_line,
	     fperfdata ("load1", la1, "", (int)wload1, wload1, (int)cload1, cload1, TRUE, 0, FALSE, 0),
	     fperfdata ("load5", la5, "", (int)wload5, wload5, (int)cload5, cload5, TRUE, 0, FALSE, 0),
	     fperfdata ("load15", la15, "", (int)wload15, wload15, (int)cload15, cload15, TRUE, 0, FALSE, 0));
	return STATE_OK;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 0;

	int option = 0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "Vhc:w:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				wload1 = atof (optarg);
				wload5 = atof (optarg);
				wload15 = atof (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
								 sscanf (optarg, "%f,%f,%f", &wload1, &wload5, &wload15) == 3)
				break;
			else if (strstr (optarg, ":") &&
							 sscanf (optarg, "%f:%f:%f", &wload1, &wload5, &wload15) == 3)
				break;
			else
				usage (_("Warning threshold must be float or float triplet!\n"));
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				cload1 = atof (optarg);
				cload5 = atof (optarg);
				cload15 = atof (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
							 sscanf (optarg, "%f,%f,%f", &cload1, &cload5, &cload15) == 3)
				break;
			else if (strstr (optarg, ":") &&
							 sscanf (optarg, "%f:%f:%f", &cload1, &cload5, &cload15) == 3)
				break;
			else
				usage (_("Critical threshold must be float or float triplet!\n"));
			break;
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();
	if (wload1 < 0 && is_nonnegative (argv[c]))
		wload1 = atof (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (cload1 < 0 && is_nonnegative (argv[c]))
		cload1 = atof (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (wload5 < 0 && is_nonnegative (argv[c]))
		wload5 = atof (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (cload5 < 0 && is_nonnegative (argv[c]))
		cload5 = atof (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (wload15 < 0 && is_nonnegative (argv[c]))
		wload15 = atof (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (cload15 < 0 && is_nonnegative (argv[c]))
		cload15 = atof (argv[c++]);

	return validate_arguments ();
}



int
validate_arguments (void)
{
	if (wload1 < 0)
		usage (_("Warning threshold for 1-minute load average is not specified\n"));
	if (wload5 < 0)
		usage (_("Warning threshold for 5-minute load average is not specified\n"));
	if (wload15 < 0)
		usage (_("Warning threshold for 15-minute load average is not specified\n"));
	if (cload1 < 0)
		usage (_("Critical threshold for 1-minute load average is not specified\n"));
	if (cload5 < 0)
		usage (_("Critical threshold for 5-minute load average is not specified\n"));
	if (cload15 < 0)
		usage (_("Critical threshold for 15-minute load average is not specified\n"));
	if (wload1 > cload1)
		usage (_("Parameter inconsistency: 1-minute \"warning load\" greater than \"critical load\".\n"));
	if (wload5 > cload5)
		usage (_("Parameter inconsistency: 5-minute \"warning load\" greater than \"critical load\".\n"));
	if (wload15 > cload15)
		usage (_("Parameter inconsistency: 15-minute \"warning load\" greater than \"critical load\".\n"));
	return OK;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin tests the current system load average.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_("\
 -w, --warning=WLOAD1,WLOAD5,WLOAD15\n\
   Exit with WARNING status if load average exceeds WLOADn\n\
 -c, --critical=CLOAD1,CLOAD5,CLOAD15\n\
   Exit with CRITICAL status if load average exceed CLOADn\n\n\
the load average format is the same used by \"uptime\" and \"w\"\n\n"));

	printf (_(UT_SUPPORT));
}

void
print_usage (void)
{
	printf (_("Usage: %s -w WLOAD1,WLOAD5,WLOAD15 -c CLOAD1,CLOAD5,CLOAD15\n"),
	        progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
