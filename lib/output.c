#include "output.h"
#include "./utils_base.h"
#include "../plugins/utils.h"

#include "assert.h"

static char *fmt_subcheck_output(mp_output_format output_format, mp_subcheck check, unsigned int indentation);

static inline char *fmt_subcheck_perfdata(mp_subcheck check) {
	char *result = strdup("");
	int added = 0;

	if (check.perfdata != NULL) {
		added = xasprintf(&result, "%s", pd_list_to_string(*check.perfdata));
	}

	if (check.subchecks == NULL) {
		// No subchecks, return here
		return result;
	}

	mp_subcheck_list *subchecks = check.subchecks;

	while (subchecks != NULL) {
		if (added > 0) {
			added = xasprintf(&result, "%s %s", result, fmt_subcheck_perfdata(subchecks->subcheck));
		} else {
			// TODO free previous result here?
			added = xasprintf(&result, "%s", result, fmt_subcheck_perfdata(subchecks->subcheck));
		}

		subchecks = subchecks->next;
	}

	return result;
}

mp_check mp_check_init() {
	mp_check check = { 0 };
	check.format = ICINGA2_FORMAT;
	return check;
}

mp_subcheck mp_subcheck_init() {
	mp_subcheck tmp = { 0 };
	tmp.default_state = STATE_UNKNOWN; // Default state is unknown
	tmp.state_set_explicitly = false;
	return tmp;
}

/* */
int mp_add_subcheck_to_check(mp_check check[static 1], mp_subcheck sc) {
	assert(sc.output != NULL); // There must be output in a subcheck
	/*
	if (sc.output == NULL ) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__, __func__,	__LINE__,
			"Sub check output is NULL"
			);
	}
	*/

	mp_subcheck_list *tmp = NULL;

	if (check->subchecks == NULL) {
		check->subchecks =  (mp_subcheck_list *)calloc(1, sizeof(mp_subcheck_list));
		if (check->subchecks == NULL ) {
			die(STATE_UNKNOWN,
					"%s - %s #%d: %s",
					__FILE__, __func__, __LINE__,
					"malloc failed"
			   );
		}

		tmp = check->subchecks;
	} else {
		// Search for the end
		tmp = check->subchecks;

		while (tmp->next != NULL) {
			tmp = tmp->next;
		}

		tmp->next =  (mp_subcheck_list *)calloc(1, sizeof(mp_subcheck_list));
		if (tmp->next == NULL ) {
			die(STATE_UNKNOWN,
					"%s - %s #%d: %s",
					__FILE__, __func__, __LINE__,
					"malloc failed"
			   );
		}
	}

	tmp->subcheck = sc;

	return 0;
}

void mp_add_perfdata_to_subcheck(mp_subcheck check[static 1], const mp_perfdata pd) {
	if (check->perfdata == NULL) {
		check->perfdata = pd_list_init();
	}
	pd_list_append(check->perfdata, pd);
}

/* */
int mp_add_subcheck_to_subcheck(mp_subcheck check[static 1], mp_subcheck sc) {
	if (sc.output == NULL ) {
		die(STATE_UNKNOWN,
			"%s - %s #%d: %s",
			__FILE__, __func__,	__LINE__,
			"Sub check output is NULL"
			);
	}

	mp_subcheck_list *tmp = NULL;

	if (check->subchecks == NULL) {
		check->subchecks =  (mp_subcheck_list *)calloc(1, sizeof(mp_subcheck_list));
		if (check->subchecks == NULL ) {
			die(STATE_UNKNOWN,
					"%s - %s #%d: %s",
					__FILE__, __func__, __LINE__,
					"malloc failed"
			   );
		}

		tmp = check->subchecks;
	} else {
		// Search for the end
		tmp = check->subchecks;

		while (tmp->next != NULL) {
			tmp = tmp->next;
		}

		tmp->next =  (mp_subcheck_list *)calloc(1, sizeof(mp_subcheck_list));
		if (tmp->next == NULL ) {
			die(STATE_UNKNOWN,
					"%s - %s #%d: %s",
					__FILE__, __func__, __LINE__,
					"malloc failed"
			   );
		}
	}

	tmp->subcheck = sc;

	return 0;
}

void mp_add_summary(mp_check check[static 1], char *summary) {
	check->summary = summary;
}

char *get_subcheck_summary(mp_check check) {
	mp_subcheck_list *subchecks = check.subchecks;

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
				die(STATE_UNKNOWN, "Unknown state in get_subcheck_summary");
		}
		subchecks = subchecks->next;
	}
	char *result = NULL;
	xasprintf(&result, "ok=%d, warning=%d, critical=%d, unknown=%d", ok, warning, critical, unknown);
	return result;
}

mp_state_enum mp_compute_subcheck_state(const mp_subcheck check) {
	mp_subcheck_list *scl = check.subchecks;

	mp_state_enum result = check.default_state;
	if (check.state_set_explicitly) {
		result = check.state;
	}

	while (scl != NULL) {
		result = max_state_alt(result, mp_compute_subcheck_state(scl->subcheck));
		scl = scl->next;
	}

	return result;
}

mp_state_enum mp_compute_check_state(const mp_check check) {
	assert(check.subchecks != NULL); // a mp_check without subchecks is invalid, die here

	mp_subcheck_list *scl = check.subchecks;
	mp_state_enum result = STATE_OK;

	while (scl != NULL) {
		result = max_state_alt(result, mp_compute_subcheck_state(scl->subcheck));
		scl = scl->next;
	}

	return result;
}

char *mp_fmt_output(mp_check check) {
	char *result = NULL;

	switch (check.format)  {
		case SUMMARY_ONLY:
			if (check.summary == NULL) {
				check.summary = get_subcheck_summary(check);
			}

			xasprintf(&result,"%s: %s", state_text(mp_compute_check_state(check)), check.summary);
			return result;

		case CLASSIC_FORMAT:
			{
				/* SERVICE STATUS: First line of output | First part of performance data
				 * Any number of subsequent lines of output, but note that buffers
				 * may have a limited size | Second part of performance data, which
				 * may have continuation lines, too
				 */
				if (check.summary == NULL) {
					check.summary = get_subcheck_summary(check);
				}

				xasprintf(&result,"%s: %s", state_text(mp_compute_check_state(check)), check.summary);

				mp_subcheck_list *subchecks = check.subchecks;

				while (subchecks != NULL) {
					xasprintf(&result, "%s - %s", result, fmt_subcheck_output(CLASSIC_FORMAT, subchecks->subcheck, 1));
					subchecks = subchecks->next;
				}

				break;
			}
		case ICINGA2_FORMAT:
			{
				if (check.summary == NULL) {
					check.summary = get_subcheck_summary(check);
				}

				xasprintf(&result,"[%s] - %s", state_text(mp_compute_check_state(check)), check.summary);

				mp_subcheck_list *subchecks = check.subchecks;

				while (subchecks != NULL) {
					xasprintf(&result, "%s\n%s", result, fmt_subcheck_output(ICINGA2_FORMAT, subchecks->subcheck, 1));
					subchecks = subchecks->next;
				}

				break;
			}
		default:
			die(STATE_UNKNOWN, "Invalid format");
	}

	char *pd_string = NULL;
	mp_subcheck_list *subchecks = check.subchecks;

	while (subchecks != NULL) {
		if (pd_string == NULL) {
			xasprintf(&pd_string, "%s", fmt_subcheck_perfdata(subchecks->subcheck));
		} else {
			xasprintf(&pd_string, "%s %s", pd_string, fmt_subcheck_perfdata(subchecks->subcheck));
		}

		subchecks = subchecks->next;
	}

	if (pd_string != NULL && strlen(pd_string) > 0) {
		xasprintf(&result, "%s|%s\n", result, pd_string);
	}

	return result;
}

static char *generate_indentation_string(unsigned int indentation) {
	char *result = calloc(indentation + 1, sizeof(char)) ;

	for (unsigned int i=0; i<indentation; i++) {
		result[i] = '\t';
	}

	return result;
}

static inline char *fmt_subcheck_output(mp_output_format output_format, mp_subcheck check, unsigned int indentation) {
	char *result = NULL;

	switch (output_format) {
		case ICINGA2_FORMAT:
			xasprintf(&result, "%s\\_[%s] - %s\n", generate_indentation_string(indentation), state_text(check.state), check.output);

			mp_subcheck_list *subchecks = check.subchecks;

			while (subchecks != NULL) {
				xasprintf(&result, "%s%s", result, fmt_subcheck_output(output_format, subchecks->subcheck, indentation+1));
				subchecks = subchecks->next;
			}
			return result;
		case CLASSIC_FORMAT:
			return result;
		case SUMMARY_ONLY:
			return result;
		default:
			die(STATE_UNKNOWN, "Invalid format");
	}
}

void mp_print_output(mp_check check) {
	puts(mp_fmt_output(check));
}

void mp_exit(mp_check check) {
	mp_print_output(check);
	exit(mp_compute_check_state(check));
}

mp_subcheck mp_set_subcheck_state(mp_subcheck check , mp_state_enum state) {
	check.state = state;
	check.state_set_explicitly = true;
	return check;
}

mp_subcheck mp_set_subcheck_default_state(mp_subcheck check , mp_state_enum state) {
	check.default_state = state;
	return check;
}
