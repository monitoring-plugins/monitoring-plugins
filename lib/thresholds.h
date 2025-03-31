#pragma once

#include "./perfdata.h"
#include "states.h"

/*
 * Old threshold type using the old range type
 */
typedef struct thresholds_struct {
	range *warning;
	range *critical;
} thresholds;

typedef struct mp_thresholds_struct {
	bool warning_is_set;
	mp_range warning;
	bool critical_is_set;
	mp_range critical;
} mp_thresholds;

mp_thresholds mp_thresholds_init(void);

mp_perfdata mp_pd_set_thresholds(mp_perfdata /* pd */, mp_thresholds /* th */);

mp_state_enum mp_get_pd_status(mp_perfdata /* pd */);

mp_thresholds mp_thresholds_set_warn(mp_thresholds thlds, mp_range warn);
mp_thresholds mp_thresholds_set_crit(mp_thresholds thlds, mp_range crit);

char *fmt_threshold_warning(thresholds th);
char *fmt_threshold_critical(thresholds th);
