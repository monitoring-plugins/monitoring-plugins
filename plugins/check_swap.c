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
	bool is_percentage;
	uint64_t value;
} threshold;

typedef struct {
	unsigned long long free; // Free swap in Bytes!
	unsigned long long used; // Used swap in Bytes!
	unsigned long long total; // Total swap size, you guessed it, in Bytes!
} swap_metrics;

typedef struct {
	int errorcode;
	int statusCode;
	swap_metrics metrics;
} swap_result;

typedef struct {
	int verbose;
	bool allswaps;
	int no_swap_state;
	threshold warn;
	threshold crit;
} swap_config;

typedef struct {
	int errorcode;
	swap_config config;
} swap_config_wrapper;

swap_config_wrapper process_arguments (swap_config_wrapper config, int argc, char **argv);
void print_usage ();
void print_help (swap_config);

swap_result getSwapFromProcMeminfo(swap_config config);
swap_result getSwapFromSwapCommand(swap_config config);
swap_result getSwapFromSwapctl_BSD(swap_config config);
swap_result getSwapFromSwap_SRV4(swap_config config);

swap_config swap_config_init() {
	swap_config tmp = { 0 };
	tmp.allswaps = false;
	tmp.no_swap_state = STATE_CRITICAL;
	tmp.verbose = 0;

	return tmp;
}


int main (int argc, char **argv) {
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	char *status;
	status = strdup ("");

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	swap_config_wrapper tmp = {
		.errorcode = OK
	};

	tmp.config = swap_config_init();

	tmp = process_arguments (tmp, argc, argv);

	if (tmp.errorcode != OK) {
		usage4 (_("Could not parse arguments"));
	}

	swap_config config = tmp.config;

#ifdef HAVE_PROC_MEMINFO
	swap_result data = getSwapFromProcMeminfo(config);
#else
# ifdef HAVE_SWAP
	swap_result data = getSwapFromSwapCommand();
# else
#  ifdef CHECK_SWAP_SWAPCTL_SVR4
	swap_result data = getSwapFromSwapctl_SRV4();
#  else
#   ifdef CHECK_SWAP_SWAPCTL_BSD
	swap_result data = getSwapFromSwapctl_BSD();
#   else
	#error No now found to retrieve swap
#   endif /* CHECK_SWAP_SWAPCTL_BSD */
#  endif /* CHECK_SWAP_SWAPCTL_SVR4 */
# endif /* HAVE_SWAP */
#endif /* HAVE_PROC_MEMINFO */

	double percent_used;

	/* if total_swap_mb == 0, let's not divide by 0 */
	if(data.metrics.total != 0) {
		percent_used = 100 * ((double) data.metrics.used) / ((double) data.metrics.total);
	} else {
		printf (_("SWAP %s - Swap is either disabled, not present, or of zero size."),
			state_text (data.statusCode));
		exit(config.no_swap_state);
	}

	uint64_t warn_print = config.warn.value;
	if (config.warn.is_percentage) {
		warn_print =
			config.warn.value * (data.metrics.total / 100);
	}

	uint64_t crit_print = config.crit.value;
	if (config.crit.is_percentage) {
		crit_print =
			config.crit.value * (data.metrics.total  / 100);
	}

	char *perfdata = perfdata_uint64(
		"swap",
		data.metrics.free,
		"B",
		true, warn_print,
		true, crit_print,
		true, 0,
		true, (long)data.metrics.total);

	if (config.verbose > 1) {
		printf("Warn threshold value: %"PRIu64"\n", config.warn.value);
	}

	if ((config.warn.is_percentage && (percent_used >= (100 - config.warn.value))) ||
			config.warn.value >= data.metrics.free) {
			data.statusCode = max_state (data.statusCode, STATE_WARNING);
		}

	if ((config.crit.is_percentage && (percent_used >= (100 - config.crit.value))) ||
			config.crit.value >= data.metrics.free) {
			data.statusCode = max_state (data.statusCode, STATE_CRITICAL);
		}

	printf (_("SWAP %s - %g%% free (%lluMB out of %lluMB) %s|%s\n"),
			state_text (data.statusCode),
			(100 - percent_used), data.metrics.free, data.metrics.total, status,
			perfdata);

	exit(data.statusCode);
}

/* process command-line arguments */
swap_config_wrapper process_arguments (swap_config_wrapper conf_wrapper, int argc, char **argv) {
	if (argc < 2) {
		conf_wrapper.errorcode = ERROR;
		return conf_wrapper;
	}

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

	int c = 0;  /* option character */
	while (true) {
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
					conf_wrapper.config.warn.is_percentage = true;
					optarg[length - 1] = '\0';
					if (is_uint64(optarg, &conf_wrapper.config.warn.value)) {
						if (conf_wrapper.config.warn.value > 100) {
							usage4 (_("Warning threshold percentage must be <= 100!"));
						}
					}
					break;
				} else {
					/* It's Bytes */
					conf_wrapper.config.warn.is_percentage = false;
					if (is_uint64(optarg, &conf_wrapper.config.warn.value)) {
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
					conf_wrapper.config.crit.is_percentage = true;
					optarg[length - 1] = '\0';
					if (is_uint64(optarg, &conf_wrapper.config.crit.value)) {
						if (conf_wrapper.config.crit.value> 100) {
							usage4 (_("Critical threshold percentage must be <= 100!"));
						}
					}
					break;
				} else {
					/* It's Bytes */
					conf_wrapper.config.crit.is_percentage = false;
					if (is_uint64(optarg, &conf_wrapper.config.crit.value)) {
						break;
					} else {
						usage4 (_("Critical threshold be positive integer or percentage!"));
					}
				}
			  }
		case 'a':									/* all swap */
			conf_wrapper.config.allswaps = true;
			break;
		case 'n':
			if ((conf_wrapper.config.no_swap_state = mp_translate_state(optarg)) == ERROR) {
				usage4 (_("no-swap result must be a valid state name (OK, WARNING, CRITICAL, UNKNOWN) or integer (0-3)."));
			}
			break;
		case 'v':									/* verbose */
			conf_wrapper.config.verbose++;
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_UNKNOWN);
		case 'h':									/* help */
			print_help (conf_wrapper.config);
			exit (STATE_UNKNOWN);
		case '?':									/* error */
			usage5 ();
		}
	}

	c = optind;

	if (conf_wrapper.config.warn.value == 0 && conf_wrapper.config.crit.value == 0) {
		conf_wrapper.errorcode = ERROR;
		return conf_wrapper;
	} else if ((conf_wrapper.config.warn.is_percentage == conf_wrapper.config.crit.is_percentage) &&
			(conf_wrapper.config.warn.value < conf_wrapper.config.crit.value)) {
		/* This is NOT triggered if warn and crit are different units, e.g warn is percentage
		 * and crit is absolute. We cannot determine the condition at this point since we
		 * dont know the value of total swap yet
		 */
		usage4(_("Warning should be more than critical"));
	}

	return conf_wrapper;
}


void
print_help (swap_config config)
{
	print_revision (progname, NP_VERSION);

	printf (_(COPYRIGHT), copyright, email);

	printf ("%s\n", _("Check swap space on local machine."));

	printf ("\n\n");

	print_usage();

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
	printf ("    %s %s\n", _("Resulting state when there is no swap regardless of thresholds. Default:"), state_text(config.no_swap_state));
	printf (UT_VERBOSE);

	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("Both INTEGER and PERCENT thresholds can be specified, they are all checked."));
	printf (" %s\n", _("On AIX, if -a is specified, uses lsps -a, otherwise uses lsps -s."));

	printf (UT_SUPPORT);
}


void
print_usage ()
{
	printf ("%s\n", _("Usage:"));
	printf (" %s [-av] -w <percent_free>%% -c <percent_free>%%\n",progname);
	printf ("  -w <bytes_free> -c <bytes_free> [-n <state>]\n");
}

#ifdef HAVE_PROC_MEMINFO
swap_result getSwapFromProcMeminfo(swap_config config) {

	if (config.verbose >= 3) {
		printf("Reading PROC_MEMINFO at %s\n", PROC_MEMINFO);
	}

	FILE *fp;
	fp = fopen (PROC_MEMINFO, "r");


	swap_result result  = { 0 };
	result.statusCode = STATE_OK;

	uint64_t swap_total = 0, swap_used = 0, swap_free = 0;

	char input_buffer[MAX_INPUT_BUFFER];
	char str[32];

	while (fgets (input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		uint64_t tmp_KB = 0;

		/*
		 * The following sscanf call looks for a line looking like: "Swap: 123 123 123"
		 * On which kind of system this format exists, I can not say, but I wanted to
		 * document this for people who are not adapt with sscanf anymore, like me
		 */
		if (sscanf(input_buffer, "%*[S]%*[w]%*[a]%*[p]%*[:] %lu %lu %lu",
				   &swap_total, &swap_used, &swap_free) == 3) {

			result.metrics.total += swap_total;
			result.metrics.used += swap_used;
			result.metrics.free += swap_free;

			/*
			 * The following sscanf call looks for lines looking like:
			 * "SwapTotal: 123" and "SwapFree: 123" This format exists at least
			 * on Debian Linux with a 5.* kernel
			 */
		} else if (sscanf(input_buffer,
						  "%*[S]%*[w]%*[a]%*[p]%[TotalFreCchd]%*[:] %lu "
						  "%*[k]%*[B]",
						  str, &tmp_KB)) {
			if (config.verbose >= 3) {
				printf("Got %s with %lu\n", str, tmp_KB);
			}
			/* I think this part is always in Kb, so convert to mb */
			if (strcmp("Total", str) == 0) {
				swap_total = tmp_KB * 1024;
			} else if (strcmp("Free", str) == 0) {
				swap_free = swap_free + tmp_KB * 1024;
			} else if (strcmp("Cached", str) == 0) {
				swap_free = swap_free + tmp_KB * 1024;
			}
		}
	}

	fclose(fp);

	result.metrics.total = swap_total;
	result.metrics.used = swap_total - swap_free;
	result.metrics.free = swap_free;

	return result;
}
#endif

#ifdef HAVE_SWAP
swap_result getSwapFromSwapCommand() {
	swap_result result  = { 0 };

	char *temp_buffer;
	char *swap_command;
	char *swap_format;
	int conv_factor = SWAP_CONVERSION;

	xasprintf(&swap_command, "%s", SWAP_COMMAND);
	xasprintf(&swap_format, "%s", SWAP_FORMAT);

/* These override the command used if a summary (and thus ! allswaps) is required */
/* The summary flag returns more accurate information about swap usage on these OSes */
# ifdef _AIX
	if (!allswaps) {
		xasprintf(&swap_command, "%s", "/usr/sbin/lsps -s");
		xasprintf(&swap_format, "%s", "%lu%*s %lu");
		conv_factor = 1;
	}
# endif

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
# ifdef _AIX
	if (!allswaps) {
		fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process);	/* Ignore first line */
		sscanf (input_buffer, swap_format, &total_swap_mb, &used_swap_mb);
		free_swap_mb = total_swap_mb * (100 - used_swap_mb) /100;
		used_swap_mb = total_swap_mb - free_swap_mb;
		if (verbose >= 3)
			printf (_("total=%.0f, used=%.0f, free=%.0f\n"), total_swap_mb, used_swap_mb, free_swap_mb);
	} else {
# endif
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
}
#endif // HAVE_SWAP

#ifdef CHECK_SWAP_SWAPCTL_BSD
swap_result getSwapFromSwapctl_BSD() {
	int i=0, nswaps=0, swapctl_res=0;
	struct swapent *ent;
	int conv_factor = SWAP_CONVERSION;

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
}
#endif // CHECK_SWAP_SWAPCTL_BSD

#ifdef CHECK_SWAP_SWAPCTL_SVR4
swap_result getSwapFromSwap_SRV4() {
	int i=0, nswaps=0, swapctl_res=0;
	swaptbl_t *tbl=NULL;
	swapent_t *ent=NULL;
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
}
#endif // CHECK_SWAP_SWAPCTL_SVR4
