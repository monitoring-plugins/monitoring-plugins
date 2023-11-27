#include "./perfdata.h"
#include "../plugins/common.h"
#include "../plugins/utils.h"

#include <assert.h>

char *pd_value_to_string(const perfdata_value pd) {
	char *result = NULL;

	switch (pd.type) {
	case PD_TYPE_INT:
		xasprintf(&result, "%lli", pd.pd_int);
		break;
	case PD_TYPE_DOUBLE:
		xasprintf(&result, "%f", pd.pd_int);
		break;
	default:
		// die here
		die(STATE_UNKNOWN, "Invalid perfdata mode\n");
	}

	return result;
}

char *pd_to_string(perfdata pd) {
	char *result = NULL;
	xasprintf(&result, "%s=", pd.label);

	xasprintf(&result, "%s%s", result, pd_value_to_string(pd.value));

	if (pd.uom != NULL) {
		xasprintf(&result, "%s", pd.uom);
	}

	if (pd.warn_present) {
		xasprintf(&result, "%s;%s", result, range_to_string(pd.warn));
	} else {
		xasprintf(&result, "%s;", result);
	}

	if (pd.crit_present) {
		xasprintf(&result, "%s;%lli", result, range_to_string(pd.crit));
	} else {
		xasprintf(&result, "%s;", result);
	}
	if (pd.min_present) {
		xasprintf(&result, "%s;%s", result, pd_value_to_string(pd.min));
	} else {
		xasprintf(&result, "%s;", result);
	}

	if (pd.max_present)
		xasprintf(&result, "%s;%s", result, pd_value_to_string(pd.max));

	/*printf("pd_to_string: %s\n", result); */

	return result;
}

char *pd_list_to_string(const pd_list pd) {
	char *result = NULL;

	for (const pd_list *elem = &pd; elem != NULL; elem = elem->next) {
		if (result != NULL) {
			xasprintf(&result, "%s %s", result, pd_to_string(elem->data));
		} else {
			xasprintf(&result, "%s", pd_to_string(elem->data));
		}
	}

	return result;
}

perfdata init_perfdata() {
	perfdata pd;
	memset(&pd, 0, sizeof(perfdata));
	return pd;

}

pd_list *new_pd_list() {
	pd_list *tmp = (pd_list *) calloc(sizeof(pd_list), 1);
	if (tmp == NULL) {
		die(STATE_UNKNOWN, "calloc failed\n");
	}
	tmp->next = NULL;
	return tmp;
}

void pd_list_append(pd_list *pdl, const perfdata pd) {
	/* Find the end */
	if (pdl == NULL) {
		pd_list *tmp = new_pd_list();
		pdl = tmp;
	} else {
		pd_list *curr = pdl->next;
		while (curr != NULL) {
			pdl = curr;
			curr = pdl->next;
		}
	}
	pd_list *tmp = new_pd_list();
	memcpy(&tmp->data, (void *)&pd, sizeof(perfdata));
	pdl->next = tmp;
}

void pd_list_free(pd_list *pdl) {
	while (pdl != NULL) {
		pd_list *old = pdl;
		pdl = pdl->next;
		free (old);
	}
}

/*
 * returns -1 if a < b, 0 if a == b, 1 if a > b
 */
int cmp_perfdata_value(const perfdata_value a, const perfdata_value b) {
	// Test if types are different
	assert(a.type == b.type);

	switch (a.type) {
		case PD_TYPE_INT:
			if (a.pd_int < b.pd_int) {
				return -1;
			} else if (a.pd_int == b.pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		case PD_TYPE_DOUBLE:
			if (a.pd_int < b.pd_int) {
				return -1;
			} else if (a.pd_int == b.pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		default:
			die(STATE_UNKNOWN, "Error in %s line: %d!", __FILE__, __LINE__);
	}
}

char *range_to_string(const range input) {
	char *result = NULL;
	if (input.alert_on == INSIDE) {
		xasprintf(&result, "@");
	}

	if (input.start_infinity) {
		xasprintf(&result, "~:");
	} else  {
		switch (input.start.type) {
			case PD_TYPE_INT:
				if (input.start.pd_int != 0) {
					xasprintf(&result, "%i:", input.start.pd_int);
				}
				break;
			case PD_TYPE_DOUBLE:
				if (input.start.pd_double != 0) {
					xasprintf(&result, "%f:", input.start.pd_double);
				}
				break;
			default:
				assert(false);
		}
	}

	if (!input.end_infinity) {
		switch (input.end.type) {
			case PD_TYPE_INT:
				xasprintf(&result, "%i:", input.end.pd_int);
				break;
			case PD_TYPE_DOUBLE:
				xasprintf(&result, "%f:", input.end.pd_double);
				break;
			default:
				assert(false);
		}
	}
	return result;
}
