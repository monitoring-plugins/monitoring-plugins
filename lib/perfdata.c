#include "./perfdata.h"
#include "../plugins/common.h"
#include "../plugins/utils.h"

#include <assert.h>

char *pd_value_to_string(const mp_perfdata_value pd) {
	char *result = NULL;

	assert(pd.type != PD_TYPE_NONE);

	switch (pd.type) {
	case PD_TYPE_INT:
		xasprintf(&result, "%lli", pd.pd_int);
		break;
	case PD_TYPE_DOUBLE:
		xasprintf(&result, "%f", pd.pd_double);
		break;
	default:
		// die here
		die(STATE_UNKNOWN, "Invalid mp_perfdata mode\n");
	}

	return result;
}

char *pd_to_string(mp_perfdata pd) {
	char *result = NULL;
	xasprintf(&result, "%s=", pd.label);

	xasprintf(&result, "%s%s", result, pd_value_to_string(pd.value));

	if (pd.uom != NULL) {
		xasprintf(&result, "%s%s", result, pd.uom);
	}

	if (pd.warn_present) {
		xasprintf(&result, "%s;%s", result, mp_range_to_string(pd.warn));
	} else {
		xasprintf(&result, "%s;", result);
	}

	if (pd.crit_present) {
		xasprintf(&result, "%s;%lli", result, mp_range_to_string(pd.crit));
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
	char *result = pd_to_string(pd.data);

	for (pd_list *elem = pd.next; elem != NULL; elem = elem->next) {
		xasprintf(&result, "%s %s", result, pd_to_string(elem->data));
	}

	return result;
}

mp_perfdata perfdata_init() {
	mp_perfdata pd = { 0 };
	return pd;

}

pd_list *pd_list_init() {
	pd_list *tmp = (pd_list *) calloc(sizeof(pd_list), 1);
	if (tmp == NULL) {
		die(STATE_UNKNOWN, "calloc failed\n");
	}
	tmp->next = NULL;
	return tmp;
}

void pd_list_append(pd_list *pdl, const mp_perfdata pd) {
	assert(pdl != NULL);

	if (pdl->data.value.type == PD_TYPE_NONE) {
		// first entry is still empty
		pdl->data = pd;
	} else {
		// find last element in the list
		pd_list *curr = pdl;
		pd_list *next = pdl->next;

		while (next != NULL) {
			curr = next;
			next = next->next;
		}

		if (curr->data.value.type == PD_TYPE_NONE) {
			// still empty
			curr->data = pd;
		} else {
			// new a new one
			curr->next = pd_list_init();
			curr->next->data = pd;
		}
	}
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
int cmp_perfdata_value(const mp_perfdata_value a, const mp_perfdata_value b) {
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

char *mp_range_to_string(const mp_range input) {
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

mp_perfdata mp_set_pd_value_double(mp_perfdata pd, double value) {
	pd.value.pd_double = value;
	pd.value.type = PD_TYPE_DOUBLE;
	return pd;
}

mp_perfdata mp_set_pd_value_int(mp_perfdata pd, int value) {
	return mp_set_pd_value_long_long(pd, (long long) value);
}

mp_perfdata mp_set_pd_value_long(mp_perfdata pd, long value) {
	return mp_set_pd_value_long_long(pd, (long long) value);
}

mp_perfdata mp_set_pd_value_long_long(mp_perfdata pd, long long value) {
	pd.value.pd_int = value;
	pd.value.type = PD_TYPE_INT;
	return pd;
}
