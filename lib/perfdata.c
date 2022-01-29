#include "./perfdata.h"
#include "../plugins/common.h"
#include "../plugins/utils.h"

char *pd_to_string(perfdata_t pd) {
	char *result = NULL;
	xasprintf(&result, "%s=", pd.label);
	switch (pd.type) {
	case INT:
		xasprintf(&result, "%s%lli", result, pd.value.pd_int, pd.uom);
		if (pd.uom != NULL) {
			xasprintf(&result, "%s", pd.uom);
		}
		if (pd.warn_present) {
			xasprintf(&result, "%s;%lli", result, pd.warn.pd_int);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%lli", result, pd.crit.pd_int);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%lli", result, pd.min.pd_int);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%lli", result, pd.max.pd_int);
		break;
	case UINT:
		xasprintf(&result, "%s%llu", result, pd.value.pd_uint);
		if (pd.uom != NULL) {
			xasprintf(&result, "%s", pd.uom);
		}
		if (pd.warn_present) {
			xasprintf(&result, "%s;%llu", result, pd.warn.pd_uint);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%llu", result, pd.crit.pd_uint);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%llu", result, pd.min.pd_uint);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%llu", result, pd.max.pd_uint);
		break;
	case FLOAT:
		xasprintf(&result, "%s%f", result, pd.value.pd_float);
		if (pd.uom != NULL) {
			xasprintf(&result, "%s", pd.uom);
		}
		if (pd.warn_present) {
			xasprintf(&result, "%s;%f", result, pd.warn.pd_float);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.crit_present) {
			xasprintf(&result, "%s;%f", result, pd.crit.pd_float);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.min_present) {
			xasprintf(&result, "%s;%f", result, pd.min.pd_float);
		} else {
			xasprintf(&result, "%s;", result);
		}
		if (pd.max_present)
			xasprintf(&result, "%s;%f", result, pd.max.pd_float);
		break;
	default:
		die(STATE_UNKNOWN, "Invalid perfdata mode\n");
	}

	/*printf("pd_to_string: %s\n", result); */

	return result;
}

char *pd_list_to_string(pd_list *pd) {
	if (pd == NULL || pd->next == NULL) return NULL;

	char *result = NULL;

	for (pd_list *elem = pd->next; elem != NULL; elem = elem->next) {
		if (result != NULL) {
			xasprintf(&result, "%s %s", result, pd_to_string(elem->data));
		} else {
			xasprintf(&result, "%s", pd_to_string(elem->data));
		}
	}

	return result;
}

pd_list *new_pd_list() {
	pd_list *tmp = (pd_list *) calloc(sizeof(pd_list), 1);
	if (tmp == NULL) {
		die(STATE_UNKNOWN, "calloc failed\n");
	}
	tmp->next = NULL;
	return tmp;
}

void pd_list_append(pd_list *pdl, const perfdata_t pd) {
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
	memcpy(&tmp->data, (void *)&pd, sizeof(perfdata_t));
	pdl->next = tmp;
}

void pd_list_free(pd_list *pdl) {
	while (pdl != NULL) {
		pd_list *old = pdl;
		pdl = pdl->next;
		free (old);
	}
}

int cmp_perfdata_value(perfdata_value *a, perfdata_value *b, value_type_t type) {
	switch (type) {
		case UINT:
			if (a->pd_uint < b->pd_uint) {
				return -1;
			} else if (a->pd_uint == b->pd_uint) {
				return 0;
			} else {
				return 1;
			}
			break;
		case INT:
			if (a->pd_int < b->pd_int) {
				return -1;
			} else if (a->pd_int == b->pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		case DOUBLE:
			if (a->pd_int < b->pd_int) {
				return -1;
			} else if (a->pd_int == b->pd_int) {
				return 0;
			} else {
				return 1;
			}
			break;
		default:
			die(STATE_UNKNOWN, "Error in %s line: %d!", __FILE__, __LINE__);
	}
}
