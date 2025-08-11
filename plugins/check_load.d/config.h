#pragma once

#include "output.h"
#include "thresholds.h"
typedef struct {
	mp_thresholds th_load[3];

	bool take_into_account_cpus;
	unsigned long n_procs_to_show;

	mp_output_format output_format;
	bool output_format_set;
} check_load_config;

check_load_config check_load_config_init() {
	check_load_config tmp = {
		.th_load =
			{
				mp_thresholds_init(),
				mp_thresholds_init(),
				mp_thresholds_init(),
			},

		.take_into_account_cpus = false,
		.n_procs_to_show = 0,

		.output_format_set = false,
	};
	return tmp;
}
