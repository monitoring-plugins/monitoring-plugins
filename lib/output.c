
#include "output.h"
#include "./utils_base.h"

/* */
int add_subcheck(check_t *check_object, enum state_enum state, char *output, pd_list perfdata_list) {

	if (check_object == NULL || output == NULL ) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__,
			__func__,
			__LINE__,
			"NULL pointer was given"
			);
	}

	subcheck_list_t *scl =  (subcheck_list_t *)malloc(sizeof(subcheck_list_t));
	if (scl == NULL ) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__, __func__, __LINE__,
			"malloc failed"
			);
	}
	scl->next = NULL;
	scl->subcheck.perfdata = perfdata_list;
	scl->subcheck.state = state;
	scl->subcheck.output = output;

	if (check_object->subchecks == NULL) {
		check_object->subchecks = scl;
	} else {
		// Find the end
		subcheck_list_t *current = check_object->subchecks->next;
		while (current != NULL) {
			current = current->next;
		}
		current->next = scl;
	}

	return 0;
}


int add_summary(check_t *check_object, char *summary) {
	check_object->summary = summary;
	return 0;
}

int print_output(check_t *check_object, enum output_format_t format) {
	if (check_object == NULL) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__,
			__func__,
			__LINE__,
			"NULL pointer was given"
			);
	}

	switch (format)  {
		case CLASSIC_FORMAT:
			/* SERVICE STATUS: First line of output | First part of performance data
			 * Any number of subsequent lines of output, but note that buffers
			 * may have a limited size | Second part of performance data, which
			 * may have continuation lines, too
			 */
			printf("%s: %s|%s\n", state_text(check_object->state), check_object->summary, print_pd_list(&check_object->perfdata));
			break;
		case ICINGA2_FORMAT:
			break;
		default:
			die(STATE_UNKNOWN, "Invalid format");
	}
}
