#include "./check_swap.d/check_swap.h"
#include "../popen.h"
#include "../utils.h"
#include "common.h"

extern int verbose;

swap_config swap_config_init(void) {
	swap_config tmp = {0};
	tmp.allswaps = false;
	tmp.no_swap_state = STATE_CRITICAL;
	tmp.conversion_factor = SWAP_CONVERSION;

	tmp.warn_is_set = false;
	tmp.crit_is_set = false;

	tmp.output_format_is_set = false;

#ifdef _AIX
	tmp.on_aix = true;
#else
	tmp.on_aix = false;
#endif

	return tmp;
}

swap_result get_swap_data(swap_config config) {
#ifdef HAVE_PROC_MEMINFO
	if (verbose >= 3) {
		printf("Reading PROC_MEMINFO at %s\n", PROC_MEMINFO);
	}

	return getSwapFromProcMeminfo(PROC_MEMINFO);
#else // HAVE_PROC_MEMINFO
#	ifdef HAVE_SWAP
	if (verbose >= 3) {
		printf("Using swap command %s with format: %s\n", SWAP_COMMAND, SWAP_FORMAT);
	}

	/* These override the command used if a summary (and thus ! allswaps) is
	 * required
	 * The summary flag returns more accurate information about swap usage on these
	 * OSes */
	if (config.on_aix && !config.allswaps) {

		config.conversion_factor = 1;

		return getSwapFromSwapCommand(config, "/usr/sbin/lsps -s", "%lu%*s %lu");
	} else {
		return getSwapFromSwapCommand(config, SWAP_COMMAND, SWAP_FORMAT);
	}
#	else // HAVE_SWAP
#		ifdef CHECK_SWAP_SWAPCTL_SVR4
	return getSwapFromSwapctl_SRV4();
#		else // CHECK_SWAP_SWAPCTL_SVR4
#			ifdef CHECK_SWAP_SWAPCTL_BSD
	return getSwapFromSwapctl_BSD();
#			else // CHECK_SWAP_SWAPCTL_BSD
#				error No way found to retrieve swap
#			endif /* CHECK_SWAP_SWAPCTL_BSD */
#		endif     /* CHECK_SWAP_SWAPCTL_SVR4 */
#	endif         /* HAVE_SWAP */
#endif             /* HAVE_PROC_MEMINFO */
}

swap_result getSwapFromProcMeminfo(char proc_meminfo[]) {
	FILE *meminfo_file_ptr;
	meminfo_file_ptr = fopen(proc_meminfo, "r");

	swap_result result = {0};
	result.errorcode = STATE_UNKNOWN;

	if (meminfo_file_ptr == NULL) {
		// failed to open meminfo file
		// errno should contain an error
		result.errorcode = STATE_UNKNOWN;
		return result;
	}

	uint64_t swap_total = 0;
	uint64_t swap_used = 0;
	uint64_t swap_free = 0;

	bool found_total = false;
	bool found_used = false;
	bool found_free = false;

	char input_buffer[MAX_INPUT_BUFFER];
	char str[32];

	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, meminfo_file_ptr)) {
		uint64_t tmp_KB = 0;

		/*
		 * The following sscanf call looks for a line looking like: "Swap: 123
		 * 123 123" On which kind of system this format exists, I can not say,
		 * but I wanted to document this for people who are not adapt with
		 * sscanf anymore, like me
		 * Also the units used here are unclear and probably wrong
		 */
		if (sscanf(input_buffer, "%*[S]%*[w]%*[a]%*[p]%*[:] %lu %lu %lu", &swap_total, &swap_used, &swap_free) == 3) {

			result.metrics.total += swap_total;
			result.metrics.used += swap_used;
			result.metrics.free += swap_free;

			found_total = true;
			found_free = true;
			found_used = true;

			// Set error
			result.errorcode = STATE_OK;

			/*
			 * The following sscanf call looks for lines looking like:
			 * "SwapTotal: 123" and "SwapFree: 123" This format exists at least
			 * on Debian Linux with a 5.* kernel
			 */
		} else {
			int sscanf_result = sscanf(input_buffer,
									   "%*[S]%*[w]%*[a]%*[p]%[TotalFreCchd]%*[:] %lu "
									   "%*[k]%*[B]",
									   str, &tmp_KB);

			if (sscanf_result == 2) {

				if (verbose >= 3) {
					printf("Got %s with %lu\n", str, tmp_KB);
				}

				/* I think this part is always in Kb, so convert to bytes */
				if (strcmp("Total", str) == 0) {
					swap_total = tmp_KB * 1000;
					found_total = true;
				} else if (strcmp("Free", str) == 0) {
					swap_free = swap_free + tmp_KB * 1000;
					found_free = true;
					found_used = true; // No explicit used metric available
				} else if (strcmp("Cached", str) == 0) {
					swap_free = swap_free + tmp_KB * 1000;
					found_free = true;
					found_used = true; // No explicit used metric available
				}

				result.errorcode = STATE_OK;
			}
		}
	}

	fclose(meminfo_file_ptr);

	result.metrics.total = swap_total;
	result.metrics.used = swap_total - swap_free;
	result.metrics.free = swap_free;

	if (!found_free || !found_total || !found_used) {
		result.errorcode = STATE_UNKNOWN;
	}

	return result;
}

swap_result getSwapFromSwapCommand(swap_config config, const char swap_command[], const char swap_format[]) {
	swap_result result = {0};

	char *temp_buffer;

	if (verbose >= 2) {
		printf(_("Command: %s\n"), swap_command);
	}
	if (verbose >= 3) {
		printf(_("Format: %s\n"), swap_format);
	}

	child_process = spopen(swap_command);
	if (child_process == NULL) {
		printf(_("Could not open pipe: %s\n"), swap_command);
		swap_result tmp = {
			.errorcode = STATE_UNKNOWN,
		};
		return tmp;
	}

	child_stderr = fdopen(child_stderr_array[fileno(child_process)], "r");
	if (child_stderr == NULL) {
		printf(_("Could not open stderr for %s\n"), swap_command);
	}

	char str[32] = {0};
	char input_buffer[MAX_INPUT_BUFFER];

	/* read 1st line */
	fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process);
	if (strcmp(swap_format, "") == 0) {
		temp_buffer = strtok(input_buffer, " \n");
		while (temp_buffer) {
			if (strstr(temp_buffer, "blocks")) {
				sprintf(str, "%s %s", str, "%lu");
			} else if (strstr(temp_buffer, "dskfree")) {
				sprintf(str, "%s %s", str, "%lu");
			} else {
				sprintf(str, "%s %s", str, "%*s");
			}
			temp_buffer = strtok(NULL, " \n");
		}
	}

	double total_swap_mb = 0;
	double free_swap_mb = 0;
	double used_swap_mb = 0;
	double dsktotal_mb = 0;
	double dskused_mb = 0;
	double dskfree_mb = 0;

	/*
	 * If different swap command is used for summary switch, need to read format
	 * differently
	 */
	if (config.on_aix && !config.allswaps) {
		fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process); /* Ignore first line */
		sscanf(input_buffer, swap_format, &total_swap_mb, &used_swap_mb);
		free_swap_mb = total_swap_mb * (100 - used_swap_mb) / 100;
		used_swap_mb = total_swap_mb - free_swap_mb;

		if (verbose >= 3) {
			printf(_("total=%.0f, used=%.0f, free=%.0f\n"), total_swap_mb, used_swap_mb, free_swap_mb);
		}
	} else {
		while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_process)) {
			sscanf(input_buffer, swap_format, &dsktotal_mb, &dskfree_mb);

			dsktotal_mb = dsktotal_mb / config.conversion_factor;
			/* AIX lists percent used, so this converts to dskfree in MBs */

			if (config.on_aix) {
				dskfree_mb = dsktotal_mb * (100 - dskfree_mb) / 100;
			} else {
				dskfree_mb = dskfree_mb / config.conversion_factor;
			}

			if (verbose >= 3) {
				printf(_("total=%.0f, free=%.0f\n"), dsktotal_mb, dskfree_mb);
			}

			dskused_mb = dsktotal_mb - dskfree_mb;
			total_swap_mb += dsktotal_mb;
			used_swap_mb += dskused_mb;
			free_swap_mb += dskfree_mb;
		}
	}

	result.metrics.free = free_swap_mb * 1024 * 1024;
	result.metrics.used = used_swap_mb * 1024 * 1024;
	result.metrics.total = free_swap_mb * 1024 * 1024;

	/* If we get anything on STDERR, at least set warning */
	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, child_stderr)) {
		result.statusCode = max_state(result.statusCode, STATE_WARNING);
		// TODO Set error here
	}

	/* close stderr */
	(void)fclose(child_stderr);

	/* close the pipe */
	if (spclose(child_process)) {
		result.statusCode = max_state(result.statusCode, STATE_WARNING);
		// TODO set error here
	}

	return result;
}

#ifndef CHECK_SWAP_SWAPCTL_BSD
#	define CHECK_SWAP_SWAPCTL_BSD

// Stub functionality for BSD stuff, so the compiler always sees the following BSD code

#	define SWAP_NSWAP 0
#	define SWAP_STATS 1

int bsd_swapctl(int cmd, const void *arg, int misc) {
	(void)cmd;
	(void)arg;
	(void)misc;
	return 512;
}

struct swapent {
	dev_t se_dev;           /* device id */
	int se_flags;           /* entry flags */
	int se_nblks;           /* total blocks */
	int se_inuse;           /* blocks in use */
	int se_priority;        /* priority */
	char se_path[PATH_MAX]; /* path to entry */
};

#else
#	define bsd_swapctl swapctl
#endif

swap_result getSwapFromSwapctl_BSD(swap_config config) {
	/* get the number of active swap devices */
	int nswaps = bsd_swapctl(SWAP_NSWAP, NULL, 0);

	/* initialize swap table + entries */
	struct swapent *ent = (struct swapent *)malloc(sizeof(struct swapent) * (unsigned long)nswaps);

	/* and now, tally 'em up */
	int swapctl_res = bsd_swapctl(SWAP_STATS, ent, nswaps);
	if (swapctl_res < 0) {
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}

	double dsktotal_mb = 0.0;
	double dskfree_mb = 0.0;
	double dskused_mb = 0.0;
	unsigned long long total_swap_mb = 0;
	unsigned long long free_swap_mb = 0;
	unsigned long long used_swap_mb = 0;

	for (int i = 0; i < nswaps; i++) {
		dsktotal_mb = (float)ent[i].se_nblks / (float)config.conversion_factor;
		dskused_mb = (float)ent[i].se_inuse / (float)config.conversion_factor;
		dskfree_mb = (dsktotal_mb - dskused_mb);

		if (config.allswaps && dsktotal_mb > 0) {
			double percent = 100 * (((double)dskused_mb) / ((double)dsktotal_mb));

			if (verbose) {
				printf("[%.0f (%g%%)]", dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += (unsigned long long)dsktotal_mb;
		free_swap_mb += (unsigned long long)dskfree_mb;
		used_swap_mb += (unsigned long long)dskused_mb;
	}

	/* and clean up after ourselves */
	free(ent);

	swap_result result = {0};

	result.statusCode = OK;
	result.errorcode = OK;

	result.metrics.total = total_swap_mb * 1024 * 1024;
	result.metrics.free = free_swap_mb * 1024 * 1024;
	result.metrics.used = used_swap_mb * 1024 * 1024;

	return result;
}

#ifndef CHECK_SWAP_SWAPCTL_SVR4
int srv4_swapctl(int cmd, void *arg) {
	(void)cmd;
	(void)arg;
	return 512;
}

typedef struct srv4_swapent {
	char *ste_path;   /* name of the swap	file */
	off_t ste_start;  /* starting	block for swapping */
	off_t ste_length; /* length of swap area */
	long ste_pages;   /* number of pages for swapping */
	long ste_free;    /* number of ste_pages free	*/
	long ste_flags;   /* ST_INDEL	bit set	if swap	file */
					  /* is now being deleted */
} swapent_t;

typedef struct swaptbl {
	int swt_n;                     /* number of swapents following */
	struct srv4_swapent swt_ent[]; /* array	of swt_n swapents */
} swaptbl_t;

#	define SC_LIST    2
#	define SC_GETNSWP 3

#	ifndef MAXPATHLEN
#		define MAXPATHLEN 2048
#	endif

#else
#	define srv4_swapctl swapctl
#endif

swap_result getSwapFromSwap_SRV4(swap_config config) {
	int nswaps = 0;

	/* get the number of active swap devices */
	if ((nswaps = srv4_swapctl(SC_GETNSWP, NULL)) == -1) {
		die(STATE_UNKNOWN, _("Error getting swap devices\n"));
	}

	if (nswaps == 0) {
		die(STATE_OK, _("SWAP OK: No swap devices defined\n"));
	}

	if (verbose >= 3) {
		printf("Found %d swap device(s)\n", nswaps);
	}

	/* initialize swap table + entries */
	swaptbl_t *tbl = (swaptbl_t *)malloc(sizeof(swaptbl_t) + (sizeof(swapent_t) * (unsigned long)nswaps));

	if (tbl == NULL) {
		die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}

	memset(tbl, 0, sizeof(swaptbl_t) + (sizeof(swapent_t) * (unsigned long)nswaps));
	tbl->swt_n = nswaps;

	for (int i = 0; i < nswaps; i++) {
		if ((tbl->swt_ent[i].ste_path = (char *)malloc(sizeof(char) * MAXPATHLEN)) == NULL) {
			die(STATE_UNKNOWN, _("malloc() failed!\n"));
		}
	}

	/* and now, tally 'em up */
	int swapctl_res = srv4_swapctl(SC_LIST, tbl);
	if (swapctl_res < 0) {
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}

	double dsktotal_mb = 0.0;
	double dskfree_mb = 0.0;
	double dskused_mb = 0.0;
	unsigned long long total_swap_mb = 0;
	unsigned long long free_swap_mb = 0;
	unsigned long long used_swap_mb = 0;

	for (int i = 0; i < nswaps; i++) {
		dsktotal_mb = (float)tbl->swt_ent[i].ste_pages / SWAP_CONVERSION;
		dskfree_mb = (float)tbl->swt_ent[i].ste_free / SWAP_CONVERSION;
		dskused_mb = (dsktotal_mb - dskfree_mb);

		if (verbose >= 3) {
			printf("dsktotal_mb=%.0f dskfree_mb=%.0f dskused_mb=%.0f\n", dsktotal_mb, dskfree_mb, dskused_mb);
		}

		if (config.allswaps && dsktotal_mb > 0) {
			double percent = 100 * (((double)dskused_mb) / ((double)dsktotal_mb));

			if (verbose) {
				printf("[%.0f (%g%%)]", dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += (unsigned long long)dsktotal_mb;
		free_swap_mb += (unsigned long long)dskfree_mb;
		used_swap_mb += (unsigned long long)dskused_mb;
	}

	/* and clean up after ourselves */
	for (int i = 0; i < nswaps; i++) {
		free(tbl->swt_ent[i].ste_path);
	}
	free(tbl);

	swap_result result = {0};
	result.errorcode = OK;
	result.metrics.total = total_swap_mb * 1024 * 1024;
	result.metrics.free = free_swap_mb * 1024 * 1024;
	result.metrics.used = used_swap_mb * 1024 * 1024;

	return result;
}
