#include "output.h"
#include "./utils_base.h"
#include "../plugins/utils.h"


/* Singleton for check_t, since you only ever need one */
check_t check;

void init_check() {
	memset(&check, 0, sizeof(check_t));
	check.format = ICINGA2_FORMAT;
}

/* */
int add_subcheck(enum state_enum state, char *output, pd_list *perfdata_list) {

	if (output == NULL ) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__, __func__,	__LINE__,
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

	if (check.subchecks == NULL) {
		check.subchecks = scl;
	} else {
		// Find the end
		subcheck_list_t *current = check.subchecks->next;
		while (current != NULL) {
			current = current->next;
		}
		current->next = scl;
	}

	return 0;
}

void add_summary(char *summary) {
	check.summary = summary;
}

char *get_subcheck_summary() {
	subcheck_list_t *subchecks = check.subchecks;

	unsigned int ok=0, warning=0, critical=0, unknown=0;
	while (subchecks != NULL) {
		switch (subchecks->subcheck.state) {
			case STATE_OK:
				ok++;
				break;
			case STATE_WARNING:
				warning++;
				break;
			case STATE_CRITICAL:
				critical++;
				break;
			case STATE_UNKNOWN:
				unknown++;
				break;
			default:
				die(STATE_UNKNOWN, "Unknow state in get_subcheck_summary");
		}
		subchecks = subchecks->next;
	}
	char *result = NULL;
	xasprintf(&result, "ok=%d, warning=%d, critical=%d, unknown=%d", ok, warning, critical, unknown);
	return result;
}

void print_output() {
	subcheck_list_t *subchecks;
	switch (check.format)  {
		case SUMMARY_ONLY:
			if (check.summary == NULL) {
				check.summary = get_subcheck_summary();
			}
			printf("%s: %s", state_text(check.state), check.summary);
			break;

		case CLASSIC_FORMAT:
			printf("%s\n", "Output format: Classic");
			/* SERVICE STATUS: First line of output | First part of performance data
			 * Any number of subsequent lines of output, but note that buffers
			 * may have a limited size | Second part of performance data, which
			 * may have continuation lines, too
			 */
			if (check.summary == NULL) {
				check.summary = get_subcheck_summary();
			}
			printf("%s: %s", state_text(check.state), check.summary);
			if (check.perfdata != NULL) {
				printf("|%s\n", pd_list_to_string(check.perfdata));
			} else {
				puts("");
			}

			char * pd_string = NULL;
			char * output_string = NULL;
			subchecks = check.subchecks;
			while (subchecks != NULL) {
				xasprintf(&output_string, "%s", subchecks->subcheck.output);
				xasprintf(&pd_string, "%s", pd_list_to_string(subchecks->subcheck.perfdata));
				subchecks = subchecks->next;
			}
			printf("%s|%s\n", output_string, pd_string);
			break;

		case ICINGA2_FORMAT:
			printf("%s\n", "Output format: Icinga2");
			if (check.summary == NULL) {
				check.summary = get_subcheck_summary();
			}
			printf("[%s]: %s", state_text(check.state), check.summary);
			if (check.perfdata != NULL) {
				printf("|%s\n", pd_list_to_string(check.perfdata));
			} else {
				puts("");
			}

			subchecks = check.subchecks;
			while (subchecks != NULL) {
				printf("\\_[%s]: %s", state_text(subchecks->subcheck.state), subchecks->subcheck.output);
				if (subchecks->subcheck.perfdata != NULL) {
					printf("|%s\n", pd_list_to_string(subchecks->subcheck.perfdata));
				}
				subchecks = subchecks->next;
			}
			break;
		default:
			die(STATE_UNKNOWN, "Invalid format");
	}
}

void set_output_format(enum output_format_t format) {
	check.format = format;
}
