/******************************************************************************
 *
 * CHECK_LOAD.C
 *
 * Written by Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>
 * License: GPL
 * Command line: CHECK_LOAD <wload1> <cload1> <wload5> <cload5> <wload15> <cload15>
 * First Written: 04/17/99 
 *
 * Modifications:
 * 
 * 05/18/1999 - Modified to work getloadavg where available, and use uptime
 *		where neither proc or getloadavg are found.  Also use autoconf.
 *                 mods by Karl DeBisschop (kdebiss@alum.mit.edu)
 * 07/01/1999 - Added some #DEFINEs to allow compilation under NetBSD, as
 *		suggested by Andy Doran.
 *	           mods by Ethan Galstad (nagios@nagios.org)
 * 07/17/1999 - Initialized la[] array to prevent NetBSD from complaining
 *		   mods by Ethan Galstad (nagios@nagios.org)
 * 08/18/1999 - Integrated some code with common plugin utilities
 *		   mods by Ethan Galstad (nagios@nagios.org)
 * $Date$
 * Note: The load format is the same used by "uptime" and "w"
 *
 *****************************************************************************/

#include "config.h"
#include "common.h"
#include "utils.h"

#ifdef HAVE_SYS_LOADAVG_H
#include <sys/loadavg.h>
#endif

/* needed for compilation under NetBSD, as suggested by Andy Doran */
#ifndef LOADAVG_1MIN
#define LOADAVG_1MIN	0
#define LOADAVG_5MIN	1
#define LOADAVG_15MIN	2
#endif /* !defined LOADAVG_1MIN */

#include "popen.h"
#ifdef HAVE_PROC_LOADAVG

#endif

#define PROGNAME "check_load"

int process_arguments (int argc, char **argv);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

float wload1 = -1, wload5 = -1, wload15 = -1;
float cload1 = -1, cload5 = -1, cload15 = -1;

int
main (int argc, char **argv)
{
#if HAVE_GETLOADAVG==1
	int result;
	double la[3] = { 0.0, 0.0, 0.0 };	/* NetBSD complains about unitialized arrays */
#elif HAVE_PROC_LOADAVG==1
	FILE *fp;
	char input_buffer[MAX_INPUT_BUFFER];
	char *tmp_ptr;
#else
	int result;
	char input_buffer[MAX_INPUT_BUFFER];
#endif

	float la1, la5, la15;

	if (process_arguments (argc, argv) == ERROR)
		usage ("\n");

#if HAVE_GETLOADAVG==1
	result = getloadavg (la, 3);
	if (result == -1)
		return STATE_UNKNOWN;
	la1 = la[LOADAVG_1MIN];
	la5 = la[LOADAVG_5MIN];
	la15 = la[LOADAVG_15MIN];
#elif HAVE_PROC_LOADAVG==1
	fp = fopen (PROC_LOADAVG, "r");
	if (fp == NULL) {
		printf ("Error opening %s\n", PROC_LOADAVG);
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
#else
	child_process = spopen (PATH_TO_UPTIME);
	if (child_process == NULL) {
		printf ("Error opening %s\n", PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL) {
		printf ("Could not open stderr for %s\n", PATH_TO_UPTIME);
	}
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	sscanf (input_buffer, "%*[^l]load average: %f, %f, %f", &la1, &la5, &la15);

	result = spclose (child_process);
	if (result) {
		printf ("Error code %d returned in %s\n", result, PATH_TO_UPTIME);
		return STATE_UNKNOWN;
	}
#endif

	if ((la1 == -1) || (la5 == -1) || (la15 == -1)) {
#if HAVE_GETLOADAVG==1
		printf ("Error in getloadavg()\n");
#elif HAVE_PROC_LOADAVG==1
		printf ("Error processing %s\n", PROC_LOADAVG);
#else
		printf ("Error processing %s\n", PATH_TO_UPTIME);
#endif
		return STATE_UNKNOWN;
	}
	printf ("load average: %.2f, %.2f, %.2f", la1, la5, la15);
	if ((la1 >= cload1) || (la5 >= cload5) || (la15 >= cload15)) {
		printf (" CRITICAL\n");
		return STATE_CRITICAL;
	}
	if ((la1 >= wload1) || (la5 >= wload5) || (la15 >= wload15)) {
		printf (" WARNING\n");
		return STATE_WARNING;
	}
	printf ("\n");
	return STATE_OK;
}





/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c, i = 0;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

#define OPTCHARS "Vhc:w:"

	if (argc < 2)
		return ERROR;

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, OPTCHARS, long_options, &option_index);
#else
		c = getopt (argc, argv, OPTCHARS);
#endif
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
				usage ("Warning threshold must be float or float triplet!\n");
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
				usage ("Critical threshold must be float or float triplet!\n");
			break;
		case 'V':									/* version */
			print_revision (my_basename (argv[0]), "$Revision$");
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage ("Invalid argument\n");
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
		usage ("Warning threshold for 1-minute load average is not specified\n");
	if (wload5 < 0)
		usage ("Warning threshold for 5-minute load average is not specified\n");
	if (wload15 < 0)
		usage ("Warning threshold for 15-minute load average is not specified\n");
	if (cload1 < 0)
		usage ("Critical threshold for 1-minute load average is not specified\n");
	if (cload5 < 0)
		usage ("Critical threshold for 5-minute load average is not specified\n");
	if (cload15 < 0)
		usage ("Critical threshold for 15-minute load average is not specified\n");
	if (wload1 > cload1)
		usage ("Parameter inconsistency: 1-minute \"warning load\" greater than \"critical load\".\n");
	if (wload5 > cload5)
		usage ("Parameter inconsistency: 5-minute \"warning load\" greater than \"critical load\".\n");
	if (wload15 > cload15)
		usage ("Parameter inconsistency: 15-minute \"warning load\" greater than \"critical load\".\n");
	return OK;
}





void
print_usage (void)
{
	printf
		("Usage: check_load -w WLOAD1,WLOAD5,WLOAD15 -c CLOAD1,CLOAD5,CLOAD15\n"
		 "       check_load --version\n" "       check_load --help\n");
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 1999 Felipe Gustavo de Almeida <galmeida@linux.ime.usp.br>\n"
		 "Copyright (c) 2000 Karl DeBisschop\n\n"
		 "This plugin tests the current system load average.\n\n");
	print_usage ();
	printf
		("\nOptions:\n"
		 " -w, --warning=WLOAD1,WLOAD5,WLOAD15\n"
		 "   Exit with WARNING status if load average exceeds WLOADn\n"
		 " -c, --critical=CLOAD1,CLOAD5,CLOAD15\n"
		 "   Exit with CRITICAL status if load average exceed CLOADn\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n"
		 "    Print version information\n\n"
		 "the load average format is the same used by \"uptime\" and \"w\"\n\n");
	support ();
}
