#include "./thresholds.h"
#include <stddef.h>
#include "stdlib.h"
#include "../plugins/utils.h"

#include "./thresholds.h"

char *range_to_string(range *input, enum value_type_t type) {
	char *result = NULL;
	if (input->alert_on == INSIDE) {
		xasprintf(&result, "@");
	}

	if (input->start_infinity) {
		xasprintf(&result, "~:");
	} else  {
		switch (type) {
			case INT64:
				if (input->start.pd_int != 0) {
					xasprintf(&result, "%i:", input->start.pd_int);
				}
				break;
			case UINT64:
				if (input->start.pd_uint != 0) {
					xasprintf(&result, "%u:", input->start.pd_uint);
				}
				break;
			case DOUBLE:
				if (input->start.pd_uint != 0) {
					xasprintf(&result, "%f:", input->start.pd_double);
				}
				break;
			default:
				exit(1);
		}
	}

	if (!input->end_infinity) {
		switch (type) {
			case INT64:
				xasprintf(&result, "%i:", input->end.pd_int);
				break;
			case UINT64:
				xasprintf(&result, "%u:", input->end.pd_uint);
				break;
			case DOUBLE:
				xasprintf(&result, "%f:", input->end.pd_double);
				break;
			default:
				exit(1);
		}
	}
	return result;
}
