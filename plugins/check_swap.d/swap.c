#include "./check_swap.d/check_swap.h"

swap_config swap_config_init() {
	swap_config tmp = {0};
	tmp.allswaps = false;
	tmp.no_swap_state = STATE_CRITICAL;
	tmp.verbose = 0;
	tmp.conversion_factor = SWAP_CONVERSION;

#ifdef _AIX
	tmp.on_aix = true;
#else
	tmp.on_aix = false;
#endif

	return tmp;
}

swap_result get_swap_data(swap_config config) {
#ifdef HAVE_PROC_MEMINFO
	if (config.verbose >= 3) {
		printf("Reading PROC_MEMINFO at %s\n", PROC_MEMINFO);
	}

	return getSwapFromProcMeminfo(config, PROC_MEMINFO);
#else
#ifdef HAVE_SWAP
	if (config.verbose >= 3) {
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
#else
#ifdef CHECK_SWAP_SWAPCTL_SVR4
	return getSwapFromSwapctl_SRV4();
#else
#ifdef CHECK_SWAP_SWAPCTL_BSD
	return getSwapFromSwapctl_BSD();
#else
#error No way found to retrieve swap
#endif /* CHECK_SWAP_SWAPCTL_BSD */
#endif /* CHECK_SWAP_SWAPCTL_SVR4 */
#endif /* HAVE_SWAP */
#endif /* HAVE_PROC_MEMINFO */
}

swap_result getSwapFromProcMeminfo(swap_config config, char proc_meminfo[]) {
	FILE *fp;
	fp = fopen(proc_meminfo, "r");

	swap_result result = {0};
	result.statusCode = STATE_OK;

	uint64_t swap_total = 0, swap_used = 0, swap_free = 0;

	char input_buffer[MAX_INPUT_BUFFER];
	char str[32];

	while (fgets(input_buffer, MAX_INPUT_BUFFER - 1, fp)) {
		uint64_t tmp_KB = 0;

		/*
		 * The following sscanf call looks for a line looking like: "Swap: 123
		 * 123 123" On which kind of system this format exists, I can not say,
		 * but I wanted to document this for people who are not adapt with
		 * sscanf anymore, like me
		 * Also the units used here are unclear and probably wrong
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

			/* I think this part is always in Kb, so convert to bytes */
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

swap_result getSwapFromSwapCommand(swap_config config, const char swap_command[], const char swap_format[]) {
	swap_result result = {0};

	char *temp_buffer;

	if (config.verbose >= 2)
		printf(_("Command: %s\n"), swap_command);
	if (config.verbose >= 3)
		printf(_("Format: %s\n"), swap_format);

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
			if (strstr(temp_buffer, "blocks"))
				sprintf(str, "%s %s", str, "%lu");
			else if (strstr(temp_buffer, "dskfree"))
				sprintf(str, "%s %s", str, "%lu");
			else
				sprintf(str, "%s %s", str, "%*s");
			temp_buffer = strtok(NULL, " \n");
		}
	}

	double total_swap_mb = 0, free_swap_mb = 0, used_swap_mb = 0;
	double dsktotal_mb = 0, dskused_mb = 0, dskfree_mb = 0;

	/*
	 * If different swap command is used for summary switch, need to read format
	 * differently
	 */
	if (config.on_aix  && !config.allswaps) {
		fgets(input_buffer, MAX_INPUT_BUFFER - 1,
			  child_process); /* Ignore first line */
		sscanf(input_buffer, swap_format, &total_swap_mb, &used_swap_mb);
		free_swap_mb = total_swap_mb * (100 - used_swap_mb) / 100;
		used_swap_mb = total_swap_mb - free_swap_mb;

		if (config.verbose >= 3) {
			printf(_("total=%.0f, used=%.0f, free=%.0f\n"), total_swap_mb,
				   used_swap_mb, free_swap_mb);
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

			if (config.verbose >= 3)
				printf(_("total=%.0f, free=%.0f\n"), dsktotal_mb, dskfree_mb);

			dskused_mb = dsktotal_mb - dskfree_mb;
			total_swap_mb += dsktotal_mb;
			used_swap_mb += dskused_mb;
			free_swap_mb += dskfree_mb;
		}
	}

	result.metrics.free = free_swap_mb * 1024 * 1024;
	result.metrics.used =  used_swap_mb * 1024 * 1024;
	result.metrics.total =  free_swap_mb  * 1024 * 1024;

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

#ifdef CHECK_SWAP_SWAPCTL_BSD
swap_result getSwapFromSwapctl_BSD(swap_config config) {
	int i = 0, nswaps = 0, swapctl_res = 0;
	struct swapent *ent;

	/* get the number of active swap devices */
	nswaps = swapctl(SWAP_NSWAP, NULL, 0);

	/* initialize swap table + entries */
	ent = (struct swapent *)malloc(sizeof(struct swapent) * nswaps);

	/* and now, tally 'em up */
	swapctl_res = swapctl(SWAP_STATS, ent, nswaps);
	if (swapctl_res < 0) {
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}


	double dsktotal_mb = 0.0, dskfree_mb = 0.0, dskused_mb = 0.0;
	unsigned long long total_swap_mb = 0, free_swap_mb = 0, used_swap_mb = 0;

	for (i = 0; i < nswaps; i++) {
		dsktotal_mb = (float)ent[i].se_nblks / config.conversion_factor;
		dskused_mb = (float)ent[i].se_inuse / config.conversion_factor;
		dskfree_mb = (dsktotal_mb - dskused_mb);

		if (config.allswaps && dsktotal_mb > 0) {
			double percent = 100 * (((double)dskused_mb) / ((double)dsktotal_mb));

			if (config.verbose) {
				printf("[%.0f (%g%%)]", dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += dsktotal_mb;
		free_swap_mb += dskfree_mb;
		used_swap_mb += dskused_mb;
	}

	/* and clean up after ourselves */
	free(ent);

	swap_result result = {0};

	result.statusCode = OK;
	result.errorcode = OK;

	result.metrics.total =  total_swap_mb * 1024 * 1024;
	result.metrics.free =  free_swap_mb * 1024 * 1024;
	result.metrics.used =  used_swap_mb * 1024 * 1024;

	return result;
}
#endif // CHECK_SWAP_SWAPCTL_BSD

#ifdef CHECK_SWAP_SWAPCTL_SVR4
swap_result getSwapFromSwap_SRV4(swap_config config) {
	int i = 0, nswaps = 0, swapctl_res = 0;
	//swaptbl_t *tbl = NULL;
	void*tbl = NULL;
	//swapent_t *ent = NULL;
	void*ent = NULL;
	/* get the number of active swap devices */
	if ((nswaps = swapctl(SC_GETNSWP, NULL)) == -1)
		die(STATE_UNKNOWN, _("Error getting swap devices\n"));

	if (nswaps == 0)
		die(STATE_OK, _("SWAP OK: No swap devices defined\n"));

	if (config.verbose >= 3)
		printf("Found %d swap device(s)\n", nswaps);

	/* initialize swap table + entries */
	tbl = (swaptbl_t *)malloc(sizeof(swaptbl_t) + (sizeof(swapent_t) * nswaps));

	if (tbl == NULL)
		die(STATE_UNKNOWN, _("malloc() failed!\n"));

	memset(tbl, 0, sizeof(swaptbl_t) + (sizeof(swapent_t) * nswaps));
	tbl->swt_n = nswaps;
	for (i = 0; i < nswaps; i++) {
		if ((tbl->swt_ent[i].ste_path =
				 (char *)malloc(sizeof(char) * MAXPATHLEN)) == NULL)
			die(STATE_UNKNOWN, _("malloc() failed!\n"));
	}

	/* and now, tally 'em up */
	swapctl_res = swapctl(SC_LIST, tbl);
	if (swapctl_res < 0) {
		perror(_("swapctl failed: "));
		die(STATE_UNKNOWN, _("Error in swapctl call\n"));
	}

	double dsktotal_mb = 0.0, dskfree_mb = 0.0, dskused_mb = 0.0;
	unsigned long long total_swap_mb = 0, free_swap_mb = 0, used_swap_mb = 0;

	for (i = 0; i < nswaps; i++) {
		dsktotal_mb = (float)tbl->swt_ent[i].ste_pages / SWAP_CONVERSION;
		dskfree_mb = (float)tbl->swt_ent[i].ste_free / SWAP_CONVERSION;
		dskused_mb = (dsktotal_mb - dskfree_mb);

		if (config.verbose >= 3)
			printf("dsktotal_mb=%.0f dskfree_mb=%.0f dskused_mb=%.0f\n",
				   dsktotal_mb, dskfree_mb, dskused_mb);

		if (config.allswaps && dsktotal_mb > 0) {
			double percent = 100 * (((double)dskused_mb) / ((double)dsktotal_mb));

			if (config.verbose) {
				printf("[%.0f (%g%%)]", dskfree_mb, 100 - percent);
			}
		}

		total_swap_mb += dsktotal_mb;
		free_swap_mb += dskfree_mb;
		used_swap_mb += dskused_mb;
	}

	/* and clean up after ourselves */
	for (i = 0; i < nswaps; i++) {
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
#endif // CHECK_SWAP_SWAPCTL_SVR4
