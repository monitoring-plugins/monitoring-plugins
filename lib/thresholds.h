#ifndef _THRESHOLDS_
#define _THRESHOLDS_

#include "./perfdata.h"

typedef struct thresholds_struct {
	range	*warning;
	range	*critical;
} thresholds;

typedef struct mp_thresholds_struct {
	mp_range	*warning;
	mp_range	*critical;
} mp_thresholds;

char *fmt_threshold_warning(const thresholds th);
char *fmt_threshold_critical(const thresholds th);

#endif /* _THRESHOLDS_ */
