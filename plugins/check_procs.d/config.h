#pragma once

#include "../../config.h"
#include "regex.h"
#include "thresholds.h"
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

enum metric {
	METRIC_PROCS,
	METRIC_VSZ,
	METRIC_RSS,
	METRIC_CPU,
	METRIC_ELAPSED
};

typedef struct {
	int options; /* bitmask of filter criteria to test against */
	enum metric metric;
	char *metric_name;
	char *input_filename;
	char *prog;
	char *args;
	char *fmt;
	char *fails;
	char *exclude_progs;
	char **exclude_progs_arr;
	char exclude_progs_counter;
	regex_t re_args;

	bool kthread_filter;
	bool usepid; /* whether to test for pid or /proc/pid/exe */
	uid_t uid;
	pid_t ppid;
	int vsz;
	int rss;
	float pcpu;
	char *statopts;

	char *warning_range;
	char *critical_range;
	thresholds *procs_thresholds;
} check_procs_config;

check_procs_config check_procs_config_init() {
	check_procs_config tmp = {
		.options = 0,
		.metric = METRIC_PROCS,
		.metric_name = strdup("PROCS"),
		.input_filename = NULL,
		.prog = NULL,
		.args = NULL,
		.fmt = NULL,
		.fails = NULL,
		.exclude_progs = NULL,
		.exclude_progs_arr = NULL,
		.exclude_progs_counter = 0,
		.re_args = {},

		.kthread_filter = false,
		.usepid = false,
		.uid = {},
		.ppid = {},
		.vsz = 0,
		.rss = 0,
		.pcpu = 0,
		.statopts = NULL,

		.warning_range = NULL,
		.critical_range = NULL,
		.procs_thresholds = NULL,
	};
	return tmp;
}
