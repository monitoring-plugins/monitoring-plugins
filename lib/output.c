#include "output.h"
#include "./utils_base.h"
#include "../plugins/utils.h"

mp_check mp_check_init() {
	mp_check check = { 0 };
	check.format = ICINGA2_FORMAT;
	return check;
}

mp_subcheck mp_subcheck_init() {
	mp_subcheck tmp = { 0 };
	tmp.perfdata = pd_list_init();
	return tmp;
}

/* */
int mp_add_subcheck(mp_check check[static 1], mp_subcheck sc) {
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

char *mp_fmt_output(mp_check check) {
	char *result = NULL;

	switch (check.format)  {
		case SUMMARY_ONLY:
			if (check.summary == NULL) {
				check.summary = get_subcheck_summary(check);
			}

			xasprintf(&result,"%s: %s", state_text(check.state), check.summary);
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

				xasprintf(&result,"%s: %s", state_text(check.state), check.summary);

				char *pd_string = NULL;

				if (check.perfdata != NULL) {
					xasprintf(&pd_string,"|%s\n", pd_list_to_string(*check.perfdata));
				}

				mp_subcheck_list *subchecks = check.subchecks;

				while (subchecks != NULL) {
					xasprintf(&result, " %s", subchecks->subcheck.output);
					xasprintf(&pd_string, " %s", pd_list_to_string(*subchecks->subcheck.perfdata));
					subchecks = subchecks->next;
				}

				xasprintf(&result, "%s|%s\n", result, pd_string);
				return result;
			}

		case ICINGA2_FORMAT:
			{
				if (check.summary == NULL) {
					check.summary = get_subcheck_summary(check);
				}

				xasprintf(&result,"[%s] - %s\n", state_text(check.state), check.summary);

				char *pd_string = NULL;

				if (check.perfdata != NULL) {
					xasprintf(&pd_string,"%s", pd_list_to_string(*check.perfdata));
				}

				mp_subcheck_list *subchecks = check.subchecks;

				while (subchecks != NULL) {
					xasprintf(&result, "%s\t\\_[%s] - %s\n", result, state_text(subchecks->subcheck.state), subchecks->subcheck.output);

					if (subchecks->subcheck.perfdata != NULL) {
						if (pd_string != NULL) {
							xasprintf(&pd_string,"%s %s", pd_string, pd_list_to_string(*subchecks->subcheck.perfdata));
						} else {
							xasprintf(&pd_string,"%s", pd_list_to_string(*subchecks->subcheck.perfdata));
						}
					}

					subchecks = subchecks->next;
				}

				xasprintf(&result, "%s|%s\n", result, pd_string);
				return result;
			}
		default:
			die(STATE_UNKNOWN, "Invalid format");
	}
}

static void print_output(mp_check check) {
	puts(mp_fmt_output(check));
}
