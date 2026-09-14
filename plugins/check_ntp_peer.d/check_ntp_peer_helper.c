#include "./check_ntp_peer_helper.h"
#include "../../config.h"
#include <ctype.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
/*
 * Extract the value from key/value pairs, or return NULL. The value returned
 * can be free()ed.
 * This function can be used to parse NTP control packet data and performance
 * data strings.
 */
char *np_extract_value(const char *varlist, const char *name, char sep) {
	char *tmp = NULL;
	char *value = NULL;

	while (true) {
		/* Strip any leading space */
		for (; isspace(varlist[0]); varlist++) {
			;
		}

		if (strncmp(name, varlist, strlen(name)) == 0) {
			varlist += strlen(name);
			/* strip trailing spaces */
			for (; isspace(varlist[0]); varlist++) {
				;
			}

			if (varlist[0] == '=') {
				/* We matched the key, go past the = sign */
				varlist++;
				/* strip leading spaces */
				for (; isspace(varlist[0]); varlist++) {
					;
				}

				if ((tmp = index(varlist, sep))) {
					/* Value is delimited by a comma */
					if (tmp - varlist == 0) {
						continue;
					}
					value = (char *)calloc(1, (unsigned long)(tmp - varlist + 1));
					strncpy(value, varlist, (unsigned long)(tmp - varlist));
					value[tmp - varlist] = '\0';
				} else {
					/* Value is delimited by a \0 */
					if (strlen(varlist) == 0) {
						continue;
					}
					value = (char *)calloc(1, strlen(varlist) + 1);
					strncpy(value, varlist, strlen(varlist));
					value[strlen(varlist)] = '\0';
				}
				break;
			}
		}
		if ((tmp = index(varlist, sep))) {
			/* More keys, keep going... */
			varlist = tmp + 1;
		} else {
			/* We're done */
			break;
		}
	}

	/* Clean-up trailing spaces/newlines */
	if (value) {
		for (unsigned long i = strlen(value) - 1; isspace(value[i]); i--) {
			value[i] = '\0';
		}
	}

	return value;
}

char *np_extract_ntpvar(const char *varlist, const char *name) {
	return np_extract_value(varlist, name, ',');
}
