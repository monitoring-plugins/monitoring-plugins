#include "./perfdata.h"
#include "../plugins/utils.h"
#include "../plugins/common.h"

char *pd_to_string(perfdata_t pd) {
	char *result = NULL;
	switch (pd.type) {
	case INT:
		xasprintf(&result, "%s%s=%lli", pd.label, pd.uom, pd.value);
		if (pd.warn_present) {
			xasprintf(&result, "%s;%lli", result, pd.warn);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%lli", result, pd.crit);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%lli", result, pd.min);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%lli", result, pd.max);
		break;
	case UINT:
		xasprintf(&result, "%s%s=%llu;", pd.label, pd.uom, pd.value);
		if (pd.warn_present) {
			xasprintf(&result, "%s;%llu", result, pd.warn);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%llu", result, pd.crit);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%llu", result, pd.min);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%llu", result, pd.max);
		break;
	case FLOAT:
		xasprintf(&result, "%s%s=%f;", pd.label, pd.uom, pd.value);
		if (pd.warn_present) {
			xasprintf(&result, "%s;%f", result, pd.warn);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%f", result, pd.crit);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%f", result, pd.min);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%f", result, pd.max);
		break;
	default:
		die(STATE_UNKNOWN, "Invalid perfdata mode\n");
	}

	return result;
}

char *pd_list_to_string(pd_list *pd) {
	if (pd == NULL) return NULL;

	char *result = NULL;

	for (pd_list *elem = pd; elem != NULL; elem = elem->next) {
		xasprintf(&result, "%s", pd_to_string(elem->data));
	}

	return result;
}
