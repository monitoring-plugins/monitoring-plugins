/*****************************************************************************
 *
 * utils_base.c
 *
 * License: GPL
 * Copyright (c) 2006 - 2024 Monitoring Plugins Development Team
 *
 * Library of useful functions for plugins
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *****************************************************************************/

#include "../plugins/common.h"
#include "states.h"
#include <stdarg.h>
#include "utils_base.h"
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>

#define np_free(ptr)                                                                               \
	{                                                                                              \
		if (ptr) {                                                                                 \
			free(ptr);                                                                             \
			ptr = NULL;                                                                            \
		}                                                                                          \
	}

monitoring_plugin *this_monitoring_plugin = NULL;

mp_state_enum timeout_state = STATE_CRITICAL;
unsigned int timeout_interval = DEFAULT_SOCKET_TIMEOUT;

bool _np_state_read_file(FILE *state_file);

void np_init(char *plugin_name, int argc, char **argv) {
	if (this_monitoring_plugin == NULL) {
		this_monitoring_plugin = calloc(1, sizeof(monitoring_plugin));
		if (this_monitoring_plugin == NULL) {
			die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
		}
		this_monitoring_plugin->plugin_name = strdup(plugin_name);
		if (this_monitoring_plugin->plugin_name == NULL) {
			die(STATE_UNKNOWN, _("Cannot execute strdup: %s"), strerror(errno));
		}
		this_monitoring_plugin->argc = argc;
		this_monitoring_plugin->argv = argv;
	}
}

void np_set_args(int argc, char **argv) {
	if (this_monitoring_plugin == NULL) {
		die(STATE_UNKNOWN, _("This requires np_init to be called"));
	}

	this_monitoring_plugin->argc = argc;
	this_monitoring_plugin->argv = argv;
}

void np_cleanup(void) {
	if (this_monitoring_plugin != NULL) {
		np_free(this_monitoring_plugin->plugin_name);
		np_free(this_monitoring_plugin);
	}
	this_monitoring_plugin = NULL;
}

/* Hidden function to get a pointer to this_monitoring_plugin for testing */
void _get_monitoring_plugin(monitoring_plugin **pointer) { *pointer = this_monitoring_plugin; }

void die(int result, const char *fmt, ...) {
	if (fmt != NULL) {
		va_list ap;
		va_start(ap, fmt);
		vprintf(fmt, ap);
		va_end(ap);
	}

	if (this_monitoring_plugin != NULL) {
		np_cleanup();
	}
	exit(result);
}

void set_range_start(range *this, double value) {
	this->start = value;
	this->start_infinity = false;
}

void set_range_end(range *this, double value) {
	this->end = value;
	this->end_infinity = false;
}

range *parse_range_string(char *str) {
	range *temp_range;
	double start;
	double end;
	char *end_str;

	temp_range = (range *)calloc(1, sizeof(range));

	/* Set defaults */
	temp_range->start = 0;
	temp_range->start_infinity = false;
	temp_range->end = 0;
	temp_range->end_infinity = true;
	temp_range->alert_on = OUTSIDE;
	temp_range->text = strdup(str);

	if (str[0] == '@') {
		temp_range->alert_on = INSIDE;
		str++;
	}

	end_str = index(str, ':');
	if (end_str != NULL) {
		if (str[0] == '~') {
			temp_range->start_infinity = true;
		} else {
			start = strtod(str, NULL); /* Will stop at the ':' */
			set_range_start(temp_range, start);
		}
		end_str++; /* Move past the ':' */
	} else {
		end_str = str;
	}
	end = strtod(end_str, NULL);
	if (strcmp(end_str, "") != 0) {
		set_range_end(temp_range, end);
	}

	if (temp_range->start_infinity || temp_range->end_infinity ||
		temp_range->start <= temp_range->end) {
		return temp_range;
	}
	free(temp_range);
	return NULL;
}

/* returns 0 if okay, otherwise 1 */
int _set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string) {
	thresholds *temp_thresholds = NULL;

	if ((temp_thresholds = calloc(1, sizeof(thresholds))) == NULL) {
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s"), strerror(errno));
	}

	temp_thresholds->warning = NULL;
	temp_thresholds->critical = NULL;

	if (warn_string != NULL) {
		if ((temp_thresholds->warning = parse_range_string(warn_string)) == NULL) {
			return NP_RANGE_UNPARSEABLE;
		}
	}
	if (critical_string != NULL) {
		if ((temp_thresholds->critical = parse_range_string(critical_string)) == NULL) {
			return NP_RANGE_UNPARSEABLE;
		}
	}

	*my_thresholds = temp_thresholds;

	return 0;
}

void set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string) {
	switch (_set_thresholds(my_thresholds, warn_string, critical_string)) {
	case 0:
		return;
	case NP_RANGE_UNPARSEABLE:
		die(STATE_UNKNOWN, _("Range format incorrect"));
	case NP_WARN_WITHIN_CRIT:
		die(STATE_UNKNOWN, _("Warning level is a subset of critical and will not be alerted"));
		break;
	}
}

void print_thresholds(const char *threshold_name, thresholds *my_threshold) {
	printf("%s - ", threshold_name);
	if (!my_threshold) {
		printf("Threshold not set");
	} else {
		if (my_threshold->warning) {
			printf("Warning: start=%g end=%g; ", my_threshold->warning->start,
				   my_threshold->warning->end);
		} else {
			printf("Warning not set; ");
		}
		if (my_threshold->critical) {
			printf("Critical: start=%g end=%g", my_threshold->critical->start,
				   my_threshold->critical->end);
		} else {
			printf("Critical not set");
		}
	}
	printf("\n");
}

/* Returns true if alert should be raised based on the range, false otherwise */
bool mp_check_range(const mp_perfdata_value value, const mp_range my_range) {
	bool is_inside = false;

	if (!my_range.end_infinity && !my_range.start_infinity) {
		// range:  .........|---inside---|...........
		// value
		is_inside = ((cmp_perfdata_value(value, my_range.start) >= 0) &&
					 (cmp_perfdata_value(value, my_range.end) <= 0));
	} else if (!my_range.start_infinity && my_range.end_infinity) {
		// range:  .........|---inside---------
		// value
		is_inside = (cmp_perfdata_value(value, my_range.start) >= 0);
	} else if (my_range.start_infinity && !my_range.end_infinity) {
		// range:  -inside--------|....................
		// value
		is_inside = (cmp_perfdata_value(value, my_range.end) == -1);
	} else {
		// range from -inf to inf, so always inside
		is_inside = true;
	}

	if ((is_inside && my_range.alert_on_inside_range == INSIDE) ||
		(!is_inside && my_range.alert_on_inside_range == OUTSIDE)) {
		return true;
	}

	return false;
}

/* Returns true if alert should be raised based on the range */
bool check_range(double value, range *my_range) {
	bool no = false;
	bool yes = true;

	if (my_range->alert_on == INSIDE) {
		no = true;
		yes = false;
	}

	if (!my_range->end_infinity && !my_range->start_infinity) {
		if ((my_range->start <= value) && (value <= my_range->end)) {
			return no;
		}
		return yes;
	}

	if (!my_range->start_infinity && my_range->end_infinity) {
		if (my_range->start <= value) {
			return no;
		}
		return yes;
	}

	if (my_range->start_infinity && !my_range->end_infinity) {
		if (value <= my_range->end) {
			return no;
		}
		return yes;
	}
	return no;
}

/* Returns status */
mp_state_enum get_status(double value, thresholds *my_thresholds) {
	if (my_thresholds->critical != NULL) {
		if (check_range(value, my_thresholds->critical)) {
			return STATE_CRITICAL;
		}
	}
	if (my_thresholds->warning != NULL) {
		if (check_range(value, my_thresholds->warning)) {
			return STATE_WARNING;
		}
	}
	return STATE_OK;
}

char *np_escaped_string(const char *string) {
	char *data;
	int write_index = 0;
	data = strdup(string);
	for (int i = 0; data[i]; i++) {
		if (data[i] == '\\') {
			switch (data[++i]) {
			case 'n':
				data[write_index++] = '\n';
				break;
			case 'r':
				data[write_index++] = '\r';
				break;
			case 't':
				data[write_index++] = '\t';
				break;
			case '\\':
				data[write_index++] = '\\';
				break;
			default:
				data[write_index++] = data[i];
			}
		} else {
			data[write_index++] = data[i];
		}
	}
	data[write_index] = '\0';
	return data;
}

int np_check_if_root(void) { return (geteuid() == 0); }

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

const char *state_text(mp_state_enum result) {
	switch (result) {
	case STATE_OK:
		return "OK";
	case STATE_WARNING:
		return "WARNING";
	case STATE_CRITICAL:
		return "CRITICAL";
	case STATE_DEPENDENT:
		return "DEPENDENT";
	default:
		return "UNKNOWN";
	}
}

/*
 * Read a string representing a state (ok, warning... or numeric: 0, 1) and
 * return the corresponding STATE_ value or ERROR)
 */
int mp_translate_state(char *state_text) {
	if (!strcasecmp(state_text, "OK") || !strcmp(state_text, "0")) {
		return STATE_OK;
	}
	if (!strcasecmp(state_text, "WARNING") || !strcmp(state_text, "1")) {
		return STATE_WARNING;
	}
	if (!strcasecmp(state_text, "CRITICAL") || !strcmp(state_text, "2")) {
		return STATE_CRITICAL;
	}
	if (!strcasecmp(state_text, "UNKNOWN") || !strcmp(state_text, "3")) {
		return STATE_UNKNOWN;
	}
	return ERROR;
}
