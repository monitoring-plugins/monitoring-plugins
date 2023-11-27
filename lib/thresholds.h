#ifndef _THRESHOLDS_
#define _THRESHOLDS_

#include "./perfdata.h"

typedef struct thresholds_struct {
	range	*warning;
	range	*critical;
} thresholds;

char *fmt_threshold_warning(const thresholds th);
char *fmt_threshold_critical(const thresholds th);

#endif /* _THRESHOLDS_ */
