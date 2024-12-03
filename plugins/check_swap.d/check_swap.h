#ifndef CHECK_SWAP_H
#define CHECK_SWAP_H

#include "../common.h"

#ifndef SWAP_CONVERSION
#	define SWAP_CONVERSION 1
#endif

typedef struct {
	bool is_percentage;
	uint64_t value;
} check_swap_threshold;

typedef struct {
	unsigned long long free;  // Free swap in Bytes!
	unsigned long long used;  // Used swap in Bytes!
	unsigned long long total; // Total swap size, you guessed it, in Bytes!
} swap_metrics;

typedef struct {
	int errorcode;
	int statusCode;
	swap_metrics metrics;
} swap_result;

typedef struct {
	bool allswaps;
	int no_swap_state;
	bool warn_is_set;
	check_swap_threshold warn;
	bool crit_is_set;
	check_swap_threshold crit;
	bool on_aix;
	int conversion_factor;
} swap_config;

swap_config swap_config_init(void);

swap_result get_swap_data(swap_config config);
swap_result getSwapFromProcMeminfo(char path_to_proc_meminfo[]);
swap_result getSwapFromSwapCommand(swap_config config, const char swap_command[], const char swap_format[]);
swap_result getSwapFromSwapctl_BSD(swap_config config);
swap_result getSwapFromSwap_SRV4(swap_config config);

#endif
