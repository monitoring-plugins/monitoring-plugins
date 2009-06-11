/*****************************************************************************
*
* utils_base.c
*
* License: GPL
* Copyright (c) 2006 Nagios Plugins Development Team
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

#include "common.h"
#include <stdarg.h>
#include "utils_base.h"

void
die (int result, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vprintf (fmt, ap);
	va_end (ap);
	exit (result);
}

void set_range_start (range *this, double value) {
	this->start = value;
	this->start_infinity = FALSE;
}

void set_range_end (range *this, double value) {
	this->end = value;
	this->end_infinity = FALSE;
}

range
*parse_range_string (char *str) {
	range *temp_range;
	double start;
	double end;
	char *end_str;

	temp_range = (range *) malloc(sizeof(range));

	/* Set defaults */
	temp_range->start = 0;
	temp_range->start_infinity = FALSE;
	temp_range->end = 0;
	temp_range->end_infinity = TRUE;
	temp_range->alert_on = OUTSIDE;

	if (str[0] == '@') {
		temp_range->alert_on = INSIDE;
		str++;
	}

	end_str = index(str, ':');
	if (end_str != NULL) {
		if (str[0] == '~') {
			temp_range->start_infinity = TRUE;
		} else {
			start = strtod(str, NULL);	/* Will stop at the ':' */
			set_range_start(temp_range, start);
		}
		end_str++;		/* Move past the ':' */
	} else {
		end_str = str;
	}
	end = strtod(end_str, NULL);
	if (strcmp(end_str, "") != 0) {
		set_range_end(temp_range, end);
	}

	if (temp_range->start_infinity == TRUE ||
		temp_range->end_infinity == TRUE ||
		temp_range->start <= temp_range->end) {
		return temp_range;
	}
	free(temp_range);
	return NULL;
}

/* returns 0 if okay, otherwise 1 */
int
_set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	thresholds *temp_thresholds = NULL;

	if ((temp_thresholds = malloc(sizeof(thresholds))) == NULL)
		die(STATE_UNKNOWN, _("Cannot allocate memory: %s\n"),
		    strerror(errno));

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

void
set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
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
	if (! my_threshold) {
		printf("Threshold not set");
	} else {
		if (my_threshold->warning) {
			printf("Warning: start=%g end=%g; ", my_threshold->warning->start, my_threshold->warning->end);
		} else {
			printf("Warning not set; ");
		}
		if (my_threshold->critical) {
			printf("Critical: start=%g end=%g", my_threshold->critical->start, my_threshold->critical->end);
		} else {
			printf("Critical not set");
		}
	}
	printf("\n");
}

/* Returns TRUE if alert should be raised based on the range */
int
check_range(double value, range *my_range)
{
	int no = FALSE;
	int yes = TRUE;

	if (my_range->alert_on == INSIDE) {
		no = TRUE;
		yes = FALSE;
	}

	if (my_range->end_infinity == FALSE && my_range->start_infinity == FALSE) {
		if ((my_range->start <= value) && (value <= my_range->end)) {
			return no;
		} else {
			return yes;
		}
	} else if (my_range->start_infinity == FALSE && my_range->end_infinity == TRUE) {
		if (my_range->start <= value) {
			return no;
		} else {
			return yes;
		}
	} else if (my_range->start_infinity == TRUE && my_range->end_infinity == FALSE) {
		if (value <= my_range->end) {
			return no;
		} else {
			return yes;
		}
	} else {
		return no;
	}
}

/* Returns status */
int
get_status(double value, thresholds *my_thresholds)
{
	if (my_thresholds->critical != NULL) {
		if (check_range(value, my_thresholds->critical) == TRUE) {
			return STATE_CRITICAL;
		}
	}
	if (my_thresholds->warning != NULL) {
		if (check_range(value, my_thresholds->warning) == TRUE) {
			return STATE_WARNING;
		}
	}
	return STATE_OK;
}

char *np_escaped_string (const char *string) {
	char *data;
	int i, j=0;
	data = strdup(string);
	for (i=0; data[i]; i++) {
		if (data[i] == '\\') {
			switch(data[++i]) {
				case 'n':
					data[j++] = '\n';
					break;
				case 'r':
					data[j++] = '\r';
					break;
				case 't':
					data[j++] = '\t';
					break;
				case '\\':
					data[j++] = '\\';
					break;
				default:
					data[j++] = data[i];
			}
		} else {
			data[j++] = data[i];
		}
	}
	data[j] = '\0';
	return data;
}

int np_check_if_root(void) { return (geteuid() == 0); }

int np_warn_if_not_root(void) {
	int status = np_check_if_root();
	if(!status) {
		printf(_("Warning: "));
		printf(_("This plugin must be either run as root or setuid root.\n"));
		printf(_("To run as root, you can use a tool like sudo.\n"));
		printf(_("To set the setuid permissions, use the command:\n"));
		/* XXX could we use something like progname? */
		printf("\tchmod u+s yourpluginfile\n");
	}
	return status;
}

/*
 * Extract the value from key/value pairs, or return NULL. The value returned
 * can be free()ed.
 * This function can be used to parse NTP control packet data and performance
 * data strings.
 */
char *np_extract_value(const char *varlist, const char *name, char sep) {
	char *tmp=NULL, *value=NULL;
	int i;

	while (1) {
		/* Strip any leading space */
		for (varlist; isspace(varlist[0]); varlist++);

		if (strncmp(name, varlist, strlen(name)) == 0) {
			varlist += strlen(name);
			/* strip trailing spaces */
			for (varlist; isspace(varlist[0]); varlist++);

			if (varlist[0] == '=') {
				/* We matched the key, go past the = sign */
				varlist++;
				/* strip leading spaces */
				for (varlist; isspace(varlist[0]); varlist++);

				if (tmp = index(varlist, sep)) {
					/* Value is delimited by a comma */
					if (tmp-varlist == 0) continue;
					value = (char *)malloc(tmp-varlist+1);
					strncpy(value, varlist, tmp-varlist);
					value[tmp-varlist] = '\0';
				} else {
					/* Value is delimited by a \0 */
					if (strlen(varlist) == 0) continue;
					value = (char *)malloc(strlen(varlist) + 1);
					strncpy(value, varlist, strlen(varlist));
					value[strlen(varlist)] = '\0';
				}
				break;
			}
		}
		if (tmp = index(varlist, sep)) {
			/* More keys, keep going... */
			varlist = tmp + 1;
		} else {
			/* We're done */
			break;
		}
	}

	/* Clean-up trailing spaces/newlines */
	if (value) for (i=strlen(value)-1; isspace(value[i]); i--) value[i] = '\0';

	return value;
}

