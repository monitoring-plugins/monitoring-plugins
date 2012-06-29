/*****************************************************************************
* 
* Nagios check_disk plugin
* 
* License: GPL
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_disk plugin
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

const char *progname = "check_swap";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "popen.h"
#include "utils.h"

#ifdef HAVE_DECL_SWAPCTL
# ifdef HAVE_SYS_PARAM_H
#  include <sys/param.h>
# endif
# ifdef HAVE_SYS_SWAP_H
#  include <sys/swap.h>
# endif
# ifdef HAVE_SYS_STAT_H
#  include <sys/stat.h>
# endif
#endif

#ifndef SWAP_CONVERSION
# define SWAP_CONVERSION 1
#endif

int check_swap (int usp, float free_swap_mb);
int process_arguments (int argc, char **argv);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

int warn_percent = 0;
int crit_percent = 0;
float warn_size_bytes = 0;
float crit_size_bytes= 0;
int verbose;
int allswaps;

int
main (int argc, char **argv)
{
	int percent_used, percent;
	float total_swap_mb = 0, used_swap_mb = 0, free_swap_mb = 0;
	float dsktotal_mb = 0, dskused_mb = 0, dskfree_mb = 0, tmp_mb = 0;
	int result = STATE_UNKNOWN;
	char input_buffer[MAX_INPUT_BUFFER];
#ifdef HAVE_PROC_MEMINFO
	FILE *fp;
#else
	int conv_factor = SWAP_CONVERSION;
# ifdef HAVE_SWAP
	char *temp_buffer;
	char *swap_command;
	char *swap_format;
# else
#  ifdef HAVE_DECL_SWAPCTL
	int i=0, nswaps=0, swapctl_res=0;
#   ifdef CHECK_SWAP_SWAPCTL_SVR4
	swaptbl_t *tbl=NULL;
	swapent_t *ent=NULL;
#   else
#    ifdef CHECK_SWAP_SWAPCTL_BSD
	struct swapent *ent;
#    endif /* CHECK_SWAP_SWAPCTL_BSD */
#   endif /* CHECK_SWAP_SWAPCTL_SVR4 */
#  endif /* HAVE_DECL_SWAPCTL */
# endif
#endif
	char str[32];
	char *status;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	status = strdup ("");

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

#ifdef HAVE_PROC_MEMINFO
	if (verbose >= 3) {
		printf("Reading PROC_MEMINFO at %s\n", PROC_MEMINFO);
	}
	fp = fopen (PROC_MEMINFO, "r");
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		if (sscanf (input_buffer, "%*[S]%*[w]%*[a]%*[p]%*[:] %f %f %f", &dsktotal_mb, &dskused_mb, &dskfree_mb) == 3) {
			dsktotal_mb = dsktotal_mb / 1048576;	/* Apply conversion */
			dskused_mb = dskused_mb / 1048576;
			dskfree_mb = dskfree_mb / 1048576;
			total_swap_mb += dsktotal_mb;
			used_swap_mb += dskused_mb;
			free_swap_mb += dskfree_mb;
			if (allswaps) {
				if (dsktotal_mb == 0)
					percent=100.0;
				else
					percent = 100 * (((double) dskused_mb) / ((double) dsktotal_mb));
				result = max_state (result, check_swap (percent, dskfree_mb));
				if (verbose)
					xasprintf (&status, "%s [%.0f (%d%%)]", status, dskfree_mb, 100 - percent);
			}
		}
		else if (sscanf (input_buffer, "%*[S]%*[w]%*[a]%*[p]%[TotalFre]%*[:] %f %*[k]%*[B]", str, &tmp_mb)) {
			if (verbose >= 3) {
				printf("Got %s with %f\n", str, tmp_mb);
			}
			/* I think this part is always in Kb, so convert to mb */
			if (strcmp ("Total", str) == 0) {
				dsktotal_mb = tmp_mb / 1024;
			}
			else if (strcmp ("Free", str) == 0) {
				dskfree_mb = tmp_mb / 1024;
			}
		}
	}
	fclose(fp);
	dskused_mb = dsktotal_mb - dskfree_mb;
	total_swap_mb = dsktotal_mb;
	used_swap_mb = dskused_mb;
	free_swap_mb = dskfree_mb;
#else
# ifdef HAVE_SWAP
	xasprintf(&swap_command, "%s", SWAP_COMMAND);
	xasprintf(&swap_format, "%s", SWAP_FORMAT);

/* These override the command used if a summary (and thus ! allswaps) is required */
/* The summary flag returns more accurate information about swap usage on these OSes */
#  ifdef _AIX
	if (!allswaps) {
		xasprintf(&swap_command, "%s", "/usr/sbin/lsps -s");
		xasprintf(&swap_format, "%s", "%f%*s %f");
		conv_factor = 1;
	}
#  endif

	if (verbose >= 2)
		printf (_("Command: %s\n"), swap_command);
	if (verbose >= 3)
		printf (_("Format: %s\n"), swap_format);

	child_process = spopen (swap_command);
	if (child_process == NULL) {
		printf (_("Could not open pipe: %s\n"), swap_command);
		return STATE_UNKNOWN;
	}

	child_stderr = fdopen (child_stderr_array[fileno (child_process)], "r");
	if (child_stderr == NULL)
		printf (_("Could not open stderr for %s\n"), swap_command);

	sprintf (str, "%s", "");
	/* read 1st line */
	fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	if (strcmp (swap_format, "") == 0) {
		temp_buffer = strtok (input_buffer, " \n");
		while (temp_buffer) {
			if (strstr (temp_buffer, "blocks"))
				sprintf (str, "%s %s", str, "%f");
			else if (strstr (temp_buffer, "dskfree"))
				sprintf (str, "%s %s", str, "%f");
			else
				sprintf (str, "%s %s", str, "%*s");
			temp_buffer = strtok (NULL, " \n");
		}
	}

/* If different swap command is used for summary switch, need to read format differently */
#  ifdef _AIX
	if (!allswaps) {
		fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process);	/* Ignore first line */
		sscanf (input_buffer, swap_format, &total_swap_mb, &used_swap_mb);
		free_swap_mb = total_swap_mb * (100 - used_swap_mb) /100;
		used_swap_mb = total_swap_mb - free_swap_mb;
		if (verbose >= 3)
			printf (_("total=%.0f, used=%.0f, free=%.0f\n"), total_swap_mb, used_swap_mb, free_swap_mb);
	} else {
#  endif
		while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
			sscanf (input_buffer, swap_format, &dsktotal_mb, &dskfree_mb);

			dsktotal_mb = dsktotal_mb / conv_factor;
			/* AIX lists percent used, so this converts to dskfree in MBs */
#  ifdef _AIX
			dskfree_mb = dsktotal_mb * (100 - dskfree_mb) / 100;
#  else
			dskfree_mb = dskfree_mb / conv_factor;
#  endif
			if (verbose >= 3)
				printf (_("total=%.0f, free=%.0f\n"), dsktotal_mb, dskfree_mb);

			dskused_mb = dsktotal_mb - dskfree_mb;
			total_swap_mb += dsktotal_mb;
			used_swap_mb += dskused_mb;
			free_swap_mb += dskfree_mb;
			if (allswaps) {
				percent = 100 * (((double) dskused_mb) / ((double) dsktotal_mb));
				result = max_state (result, check_swap (percent, dskfree_mb));
				if (verbose)
					xasprintf (&status, "%s [%.0f (%d%%)]", status, dskfree_mb, 100 - percent);
			}
		}
#  ifdef _AIX
	}
#  endif

	/* If we get anything on STDERR, at least set warning */
	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, child_stderr))
		result = max_state (result, STATE_WARNING);

	/* close stderr */
	(void) fclose (child_stderr);

	/* close the pipe */
	if (spclose (child_process))
		result = max_state (result, STATE_WARNING);
# else
#  ifdef CHECK_SWAP_SWAPCTL_SVR4

	/* get the number of active swap devices */
	if((nswaps=swapctl(SC_GETNSWP, NULL))== -1)
		die(STATE_UNKNOWN, _("Error getting swap devices\n") );

	if(nswaps == 0)
		die(STATE_OK, _("SWAP OK: No swap devices defined\n"));

	if(verbose >= 3)
		printf("Found %d swap device(s)\n", nswaps);

	/* initialize swap table + entries */
	tbl=(swaptbl_t*)malloc(sizeof(swaptbl_t)+(sizeof(swapent_t)*nswaps));

	if(tbl==NULL)
		die(STATE_UNKNOWN, _("malloc() failed!\n"));

	memset(tbl, 0, sizeof(swaptbl_t)+(sizeof(swapent_t)*nswaps));
	tbl->swt_n=nswaps;
	for(i=0;i<nswaps;i++){
		if((tbl->swt_ent[i].ste_path=(char*)malloc(sizeof(char)*MAXPATHLEN)) == NULL)
			die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}

	/* and now, tally 'em up */
	swapctl_res=swapctl(SC_LIST, tbl);
	if(swapctl_res < 0){
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}

	for(i=0;i<nswaps;i++){
		dsktotal_mb = (float) tbl->swt_ent[i].ste_pages / SWAP_CONVERSION;
		dskfree_mb = (float) tbl->swt_ent[i].ste_free /  SWAP_CONVERSION;
		dskused_mb = ( dsktotal_mb - dskfree_mb );

		if (verbose >= 3)
			printf ("dsktotal_mb=%.0f dskfree_mb=%.0f dskused_mb=%.0f\n", dsktotal_mb, dskfree_mb, dskused_mb);

		if(allswaps && dsktotal_mb > 0){
			percent = 100 * (((double) dskused_mb) / ((double) dsktotal_mb));
			result = max_state (result, check_swap (percent, dskfree_mb));
			if (verbose) {
				xasprintf (&status, "%s [%.0f (%d%%)]", status, dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += dsktotal_mb;
		free_swap_mb += dskfree_mb;
		used_swap_mb += dskused_mb;
	}

	/* and clean up after ourselves */
	for(i=0;i<nswaps;i++){
		free(tbl->swt_ent[i].ste_path);
	}
	free(tbl);
#  else
#   ifdef CHECK_SWAP_SWAPCTL_BSD

	/* get the number of active swap devices */
	nswaps=swapctl(SWAP_NSWAP, NULL, 0);

	/* initialize swap table + entries */
	ent=(struct swapent*)malloc(sizeof(struct swapent)*nswaps);

	/* and now, tally 'em up */
	swapctl_res=swapctl(SWAP_STATS, ent, nswaps);
	if(swapctl_res < 0){
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}

	for(i=0;i<nswaps;i++){
		dsktotal_mb = (float) ent[i].se_nblks / conv_factor;
		dskused_mb = (float) ent[i].se_inuse / conv_factor;
		dskfree_mb = ( dsktotal_mb - dskused_mb );

		if(allswaps && dsktotal_mb > 0){
			percent = 100 * (((double) dskused_mb) / ((double) dsktotal_mb));
			result = max_state (result, check_swap (percent, dskfree_mb));
			if (verbose) {
				xasprintf (&status, "%s [%.0f (%d%%)]", status, dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += dsktotal_mb;
		free_swap_mb += dskfree_mb;
		used_swap_mb += dskused_mb;
	}

	/* and clean up after ourselves */
	free(ent);

#   endif /* CHECK_SWAP_SWAPCTL_BSD */
#  endif /* CHECK_SWAP_SWAPCTL_SVR4 */
# endif /* HAVE_SWAP */
#endif /* HAVE_PROC_MEMINFO */

	/* if total_swap_mb == 0, let's not divide by 0 */
	if(total_swap_mb) {
		percent_used = 100 * ((double) used_swap_mb) / ((double) total_swap_mb);
	} else {
		percent_used = 0;
	}

	result = max_state (result, check_swap (percent_used, free_swap_mb));
	printf (_("SWAP %s - %d%% free (%d MB out of %d MB) %s|"),
			state_text (result),
			(100 - percent_used), (int) free_swap_mb, (int) total_swap_mb, status);

	puts (perfdata ("swap", (long) free_swap_mb, "MB",
	                TRUE, (long) max (warn_size_bytes/(1024 * 1024), warn_percent/100.0*total_swap_mb),
	                TRUE, (long) max (crit_size_bytes/(1024 * 1024), crit_percent/100.0*total_swap_mb),
	                TRUE, 0,
	                TRUE, (long) total_swap_mb));

	return result;
}



int
check_swap (int usp, float free_swap_mb)
{
	int result = STATE_UNKNOWN;
	float free_swap = free_swap_mb * (1024 * 1024);		/* Convert back to bytes as warn and crit specified in bytes */
	if (usp >= 0 && crit_percent != 0 && usp >= (100.0 - crit_percent))
		result = STATE_CRITICAL;
	else if (crit_size_bytes > 0 && free_swap <= crit_size_bytes)
		result = STATE_CRITICAL;
	else if (usp >= 0 && warn_percent != 0 && usp >= (100.0 - warn_percent))
		result = STATE_WARNING;
	else if (warn_size_bytes > 0 && free_swap <= warn_size_bytes)
		result = STATE_WARNING;
	else if (usp >= 0.0)
		result = STATE_OK;
	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c = 0;  /* option character */

	int option = 0;
	static struct option longopts[] = {
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"allswaps", no_argument, 0, 'a'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "+?Vvhac:w:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w':									/* warning size threshold */
			if (is_intnonneg (optarg)) {
				warn_size_bytes = (float) atoi (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%f,%d%%", &warn_size_bytes, &warn_percent) == 2) {
				warn_size_bytes = floorf(warn_size_bytes);
				break;
			}
			else if (strstr (optarg, "%") &&
							 sscanf (optarg, "%d%%", &warn_percent) == 1) {
				break;
			}
			else {
				usage4 (_("Warning threshold must be integer or percentage!"));
			}
		case 'c':									/* critical size threshold */
			if (is_intnonneg (optarg)) {
				crit_size_bytes = (float) atoi (optarg);
				break;
			}
			else if (strstr (optarg, ",") &&
							 strstr (optarg, "%") &&
							 sscanf (optarg, "%f,%d%%", &crit_size_bytes, &crit_percent) == 2) {
				crit_size_bytes = floorf(crit_size_bytes);
				break;
			}
			else if (strstr (optarg, "%") &&
							 sscanf (optarg, "%d%%", &crit_percent) == 1) {
				break;
			}
			else {
				usage4 (_("Critical threshold must be integer or percentage!"));
			}
		case 'a':									/* all swap */
			allswaps = TRUE;
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* error */
			usage5 ();
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();
	if (warn_percent == 0 && is_intnonneg (argv[c]))
		warn_percent = atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (crit_percent == 0 && is_intnonneg (argv[c]))
		crit_percent = atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (warn_size_bytes == 0 && is_intnonneg (argv[c]))
		warn_size_bytes = (float) atoi (argv[c++]);

	if (c == argc)
		return validate_arguments ();
	if (crit_size_bytes == 0 && is_intnonneg (argv[c]))
		crit_size_bytes = (float) atoi (argv[c++]);

	return validate_arguments ();
}



int
validate_arguments (void)
{
	if (warn_percent == 0 && crit_percent == 0 && warn_size_bytes == 0
			&& crit_size_bytes == 0) {
		return ERROR;
	}
	else if (warn_percent < crit_percent) {
		usage4
			(_("Warning percentage should be more than critical percentage"));
	}
	else if (warn_size_bytes < crit_size_bytes) {
		usage4
			(_("Warning free space should be more than critical free space"));
	}
	return OK;
}



void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf (_(COPYRIGHT), copyright, email);

	printf ("%s\n", _("Check swap space on local machine."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-w, --warning=INTEGER");
  printf ("    %s\n", _("Exit with WARNING status if less than INTEGER bytes of swap space are free"));
  printf (" %s\n", "-w, --warning=PERCENT%%");
  printf ("    %s\n", _("Exit with WARNING status if less than PERCENT of swap space is free"));
  printf (" %s\n", "-c, --critical=INTEGER");
  printf ("    %s\n", _("Exit with CRITICAL status if less than INTEGER bytes of swap space are free"));
  printf (" %s\n", "-c, --critical=PERCENT%%");
  printf ("    %s\n", _("Exit with CRITCAL status if less than PERCENT of swap space is free"));
  printf (" %s\n", "-a, --allswaps");
  printf ("    %s\n", _("Conduct comparisons for all swap partitions, one by one"));
	printf (UT_VERBOSE);

	printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("On AIX, if -a is specified, uses lsps -a, otherwise uses lsps -s."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
  printf ("%s [-av] -w <percent_free>%% -c <percent_free>%%\n",progname);
  printf ("%s [-av] -w <bytes_free> -c <bytes_free>\n", progname);
}
