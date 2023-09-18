/*****************************************************************************
*
* Monitoring check_swap plugin
*
* License: GPL
* Copyright (c) 2000 Karl DeBisschop (kdebisschop@users.sourceforge.net)
* Copyright (c) 2000-2007 Monitoring Plugins Development Team
*
* Description:
*
* This file contains the check_swap plugin
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
const char *email = "devel@monitoring-plugins.org";

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

typedef struct {
	int is_percentage;
	uint64_t value;
} threshold_t;

int check_swap (float free_swap_mb, float total_swap_mb);
int process_arguments (int argc, char **argv);
int validate_arguments (void);
void print_usage (void);
void print_help (void);

threshold_t warn;
threshold_t crit;
int verbose;
int allswaps;
int no_swap_state = STATE_CRITICAL;

int
main (int argc, char **argv)
{
	unsigned int percent_used, percent;
	uint64_t total_swap_mb = 0, used_swap_mb = 0, free_swap_mb = 0;
	uint64_t dsktotal_mb = 0, dskused_mb = 0, dskfree_mb = 0;
	uint64_t tmp_KB = 0;
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
		/*
		 * The following sscanf call looks for a line looking like: "Swap: 123 123 123"
		 * On which kind of system this format exists, I can not say, but I wanted to
		 * document this for people who are not adapt with sscanf anymore, like me
		 */
		if (sscanf (input_buffer, "%*[S]%*[w]%*[a]%*[p]%*[:] %lu %lu %lu", &dsktotal_mb, &dskused_mb, &dskfree_mb) == 3) {
			dsktotal_mb = dsktotal_mb / (1024 * 1024);	/* Apply conversion */
			dskused_mb = dskused_mb / (1024 * 1024);
			dskfree_mb = dskfree_mb / (1024 * 1024);
			total_swap_mb += dsktotal_mb;
			used_swap_mb += dskused_mb;
			free_swap_mb += dskfree_mb;
			if (allswaps) {
				if (dsktotal_mb == 0)
					percent=100.0;
				else
					percent = 100 * (((double) dskused_mb) / ((double) dsktotal_mb));
				result = max_state (result, check_swap (dskfree_mb, dsktotal_mb));
				if (verbose)
					xasprintf (&status, "%s [%lu (%d%%)]", status, dskfree_mb, 100 - percent);
			}
		}

		/*
		 * The following sscanf call looks for lines looking like: "SwapTotal: 123" and "SwapFree: 123"
		 * This format exists at least on Debian Linux with a 5.* kernel
		 */
		else if (sscanf (input_buffer, "%*[S]%*[w]%*[a]%*[p]%[TotalFreCchd]%*[:] %lu %*[k]%*[B]", str, &tmp_KB)) {
			if (verbose >= 3) {
				printf("Got %s with %lu\n", str, tmp_KB);
			}
			/* I think this part is always in Kb, so convert to mb */
			if (strcmp ("Total", str) == 0) {
				dsktotal_mb = tmp_KB / 1024;
			}
			else if (strcmp ("Free", str) == 0) {
				dskfree_mb = dskfree_mb + tmp_KB / 1024;
			}
			else if (strcmp ("Cached", str) == 0) {
				dskfree_mb = dskfree_mb + tmp_KB / 1024;
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
		xasprintf(&swap_format, "%s", "%lu%*s %lu");
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
				sprintf (str, "%s %s", str, "%lu");
			else if (strstr (temp_buffer, "dskfree"))
				sprintf (str, "%s %s", str, "%lu");
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
				result = max_state (result, check_swap (dskfree_mb, dsktotal_mb));
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
			result = max_state (result, check_swap (dskfree_mb, dsktotal_mb));
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
			result = max_state (result, check_swap(dskfree_mb, dsktotal_mb));
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
		percent_used = 100;
		status = "- Swap is either disabled, not present, or of zero size. ";
	}

	result = max_state (result, check_swap(free_swap_mb, total_swap_mb));
	printf (_("SWAP %s - %d%% free (%dMB out of %dMB) %s|"),
			state_text (result),
			(100 - percent_used), (int) free_swap_mb, (int) total_swap_mb, status);

	uint64_t warn_print = warn.value;
	if (warn.is_percentage) warn_print = warn.value * (total_swap_mb *1024 *1024/100);
	uint64_t crit_print = crit.value;
	if (crit.is_percentage) crit_print = crit.value * (total_swap_mb *1024 *1024/100);

	puts (perfdata_uint64 ("swap", free_swap_mb *1024 *1024, "B",
	                TRUE, warn_print,
	                TRUE, crit_print,
	                TRUE, 0,
	                TRUE, (long) total_swap_mb * 1024 * 1024));

	return result;
}


int
check_swap(float free_swap_mb, float total_swap_mb)
{

	if (!total_swap_mb) return no_swap_state;

	uint64_t free_swap = free_swap_mb * (1024 * 1024);		/* Convert back to bytes as warn and crit specified in bytes */

	if (!crit.is_percentage && crit.value >= free_swap) return STATE_CRITICAL;
	if (!warn.is_percentage && warn.value >= free_swap) return STATE_WARNING;


	uint64_t usage_percentage = ((total_swap_mb - free_swap_mb) / total_swap_mb) * 100;

	if (crit.is_percentage &&
			crit.value != 0 &&
			usage_percentage >= (100 - crit.value))
	{
			return STATE_CRITICAL;
	}

	if (warn.is_percentage &&
			warn.value != 0 &&
			usage_percentage >= (100 - warn.value))
	{
			return STATE_WARNING;
	}

	return STATE_OK;
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
		{"no-swap", required_argument, 0, 'n'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	while (1) {
		c = getopt_long (argc, argv, "+?Vvhac:w:n:", longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'w': /* warning size threshold */
			{
				/*
				 * We expect either a positive integer value without a unit, which means
				 * the unit is Bytes or a positive integer value and a percentage sign (%),
				 * which means the value must be with 0 and 100 and is relative to the total swap
				 */
				size_t length;
				length = strlen(optarg);

				if (optarg[length - 1] == '%') {
					/* It's percentage */
					warn.is_percentage = 1;
					optarg[length - 1] = '\0';
					if (is_uint64(optarg, &warn.value)) {
						if (warn.value > 100) {
							usage4 (_("Warning threshold percentage must be <= 100!"));
						}
					}
					break;
				} else {
					/* It's Bytes */
					warn.is_percentage = 0;
					if (is_uint64(optarg, &warn.value)) {
						break;
					} else {
						usage4 (_("Warning threshold be positive integer or percentage!"));
					}
				}
			}
		case 'c': /* critical size threshold */
			{
				/*
				 * We expect either a positive integer value without a unit, which means
				 * the unit is Bytes or a positive integer value and a percentage sign (%),
				 * which means the value must be with 0 and 100 and is relative to the total swap
				 */
				size_t length;
				length = strlen(optarg);

				if (optarg[length - 1] == '%') {
					/* It's percentage */
					crit.is_percentage = 1;
					optarg[length - 1] = '\0';
					if (is_uint64(optarg, &crit.value)) {
						if (crit.value> 100) {
							usage4 (_("Critical threshold percentage must be <= 100!"));
						}
					}
					break;
				} else {
					/* It's Bytes */
					crit.is_percentage = 0;
					if (is_uint64(optarg, &crit.value)) {
						break;
					} else {
						usage4 (_("Critical threshold be positive integer or percentage!"));
					}
				}
			  }
		case 'a':									/* all swap */
			allswaps = TRUE;
			break;
		case 'n':
			if ((no_swap_state = mp_translate_state(optarg)) == ERROR) {
				usage4 (_("no-swap result must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			}
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help ();
			exit (STATE_UNKNOWN);
		case '?':									/* error */
			usage5 ();
		}
	}

	c = optind;
	if (c == argc)
		return validate_arguments ();

	return validate_arguments ();
}



int
validate_arguments (void)
{
	if (warn.value == 0 && crit.value == 0) {
		return ERROR;
	}
	else if ((warn.is_percentage == crit.is_percentage) && (warn.value < crit.value)) {
		/* This is NOT triggered if warn and crit are different units, e.g warn is percentage
		 * and crit is absolute. We cannot determine the condition at this point since we
		 * dont know the value of total swap yet
		 */
		usage4(_("Warning should be more than critical"));
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
	printf (" %s\n", "-w, --warning=PERCENT%");
	printf ("    %s\n", _("Exit with WARNING status if less than PERCENT of swap space is free"));
	printf (" %s\n", "-c, --critical=INTEGER");
	printf ("    %s\n", _("Exit with CRITICAL status if less than INTEGER bytes of swap space are free"));
	printf (" %s\n", "-c, --critical=PERCENT%");
	printf ("    %s\n", _("Exit with CRITICAL status if less than PERCENT of swap space is free"));
	printf (" %s\n", "-a, --allswaps");
	printf ("    %s\n", _("Conduct comparisons for all swap partitions, one by one"));
	printf (" %s\n", "-n, --no-swap=<ok|warning|critical|unknown>");
	printf ("    %s %s\n", _("Resulting state when there is no swap regardless of thresholds. Default:"), state_text(no_swap_state));
	printf (UT_VERBOSE);

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("Both INTEGER and PERCENT thresholds can be specified, they are all checked."));
	printf (" %s\n", _("On AIX, if -a is specified, uses lsps -a, otherwise uses lsps -s."));

	printf (UT_SUPPORT);
}


void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf (" %s [-av] -w <percent_free>%% -c <percent_free>%%\n",progname);
	printf ("  -w <bytes_free> -c <bytes_free> [-n <state>]\n");
}
