#include "./thresholds.h"
#include "./utils_base.h"
#include "perfdata.h"

#include <stddef.h>

mp_thresholds mp_thresholds_init() {
	mp_thresholds tmp = {
		.critical = {},
		.critical_is_set = false,
		.warning = {},
		.warning_is_set = false,
	};
	return tmp;
}

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

mp_perfdata mp_pd_set_thresholds(mp_perfdata perfdata, mp_thresholds threshold) {
	if (threshold.critical_is_set) {
		perfdata.crit = threshold.critical;
		perfdata.crit_present = true;
	}

	if (threshold.warning_is_set) {
		perfdata.warn = threshold.warning;
		perfdata.warn_present = true;
	}

	return perfdata;
}

mp_state_enum mp_get_pd_status(mp_perfdata perfdata) {
	if (perfdata.crit_present) {
		if (mp_check_range(perfdata.value, perfdata.crit)) {
			return STATE_CRITICAL;
		}
	}
	if (perfdata.warn_present) {
		if (mp_check_range(perfdata.value, perfdata.warn)) {
			return STATE_WARNING;
		}
	}

	return STATE_OK;
}

mp_thresholds mp_thresholds_set_warn(mp_thresholds thlds, mp_range warn) {
	thlds.warning = warn;
	thlds.warning_is_set = true;
	return thlds;
}

mp_thresholds mp_thresholds_set_crit(mp_thresholds thlds, mp_range crit) {
	thlds.critical = crit;
	thlds.critical_is_set = true;
	return thlds;
}
