#include "./thresholds.h"

#include <stddef.h>


char *fmt_threshold_warning(const thresholds th) {
	if (th.warning == NULL) {
		return "";
	}

	return fmt_range(*th.warning);
}

char *fmt_threshold_critical(const thresholds th) {
	if (th.critical == NULL) {
		return "";
	}
	return fmt_range(*th.critical);
}
