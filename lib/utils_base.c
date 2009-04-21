/*****************************************************************************
 *
 * utils_base.c
 *
 * Library of useful functions for plugins
 * These functions are tested with libtap. See tests/ directory
 *
 * Copyright (c) 2006 Nagios Plugin Development Team
 * License: GPL
 *
 * $Revision$
 * $Date$
 ****************************************************************************/

#if HAVE_LIBGEN_H
#include <libgen.h>	/* basename(3) */
#endif
#include <stdarg.h>
#include "common.h"
#include "utils_base.h"

#define PRINT_OUTPUT(fmt, ap)                                          \
	do {                                                           \
		fmt = insert_syserr(fmt);                              \
		va_start(ap, fmt);                                     \
		vprintf(fmt, ap);                                      \
		va_end(ap);                                            \
	} while (/* CONSTCOND */ 0)

static char *insert_syserr(const char *);

extern int errno;
static int verbosity_level = -2;
static const char *program_name = NULL;
static const char *service_name = NULL;

/*
 * Set static variables for use in output functions.  Usually, argv[0] may be
 * used as progname, since we call basename(3) ourselves.  If a verbosity value
 * of -2 is specified, the verbosity_level won't be set.  Currently, no flags
 * are implemented.
 */
void
np_set_output(const char *progname, const char *servname, int verbosity,
               int flags __attribute__((unused)))
{
	static char pathbuf[128], progbuf[128], servbuf[32];

	if (progname != NULL) {
#if HAVE_BASENAME
		/*
		 * Copy the progname into a temporary buffer in order to cope
		 * with basename(3) implementations which modify their argument.
		 * TODO: Maybe we should implement an np_basename()?  Gnulib's
		 * base_name() dies on error, writing a message to stderr, which
		 * is probably not what we want.  Once we have some replacement,
		 * the libgen-/basename(3)-related checks can be removed from
		 * configure.in.
		 */
		strncpy(pathbuf, progname, sizeof(pathbuf) - 1);
		pathbuf[sizeof(pathbuf) - 1] = '\0';
		progname = basename(pathbuf);
#endif
		strncpy(progbuf, progname, sizeof(progbuf) - 1);
		progbuf[sizeof(progbuf) - 1] = '\0';
		program_name = progbuf;
	}
	if (servname != NULL) {
		strncpy(servbuf, servname, sizeof(servbuf) - 1);
		servbuf[sizeof(servbuf) - 1] = '\0';
		service_name = servbuf;
	}
	if (verbosity != -2)
		verbosity_level = verbosity;
}

int
np_adjust_verbosity(int by)
{
	if (verbosity_level == -2)
		verbosity_level = by;
	else
		verbosity_level += by;

	/* We don't support verbosity levels < -1. */
	if (verbosity_level < -1)
		verbosity_level = -1;

	return verbosity_level;
}

void
np_debug(int verbosity, const char *fmt, ...)
{
	va_list ap;

	if (verbosity_level != -1 && verbosity >= verbosity_level)
		PRINT_OUTPUT(fmt, ap);
}

void
np_verbose(const char *fmt, ...)
{
	va_list ap;

	if (verbosity_level >= 1) {
		PRINT_OUTPUT(fmt, ap);
		putchar('\n');
	}
}

void
np_die(int status, const char *fmt, ...)
{
	va_list ap;
	const char *p;

	if (program_name == NULL || service_name == NULL)
		PRINT_OUTPUT(fmt, ap);

	for (p = (verbosity_level > 0) ?
	    VERBOSE_OUTPUT_FORMAT : STANDARD_OUTPUT_FORMAT;
	    *p != '\0'; p++) {
		if (*p == '%') {
			if (*++p == '\0')
				break;
			switch (*p) {
			case 'm':
				PRINT_OUTPUT(fmt, ap);
				continue;
			case 'p':
				fputs(program_name, stdout);
				continue;
			case 's':
				fputs(service_name, stdout);
				continue;
			case 'x':
				fputs(state_text(status), stdout);
				continue;
			}
		} else if (*p == '\\') {
			if (*++p == '\0')
				break;
			switch (*p) {
			case 'n':
				putchar('\n');
				continue;
			case 't':
				putchar('\t');
				continue;
			}
		}
		putchar(*p);
	}
	exit(status);
}

/* TODO: die() can be removed as soon as all plugins use np_die() instead. */
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

	temp_thresholds = malloc(sizeof(temp_thresholds));

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

	if (*my_thresholds > 0) {	/* Not sure why, but sometimes could be -1 */
		/* printf("Freeing here: %d\n", *my_thresholds); */
		free(*my_thresholds);
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

const char *
state_text(int result)
{
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
 * Replace occurrences of "%m" by strerror(errno).  Other printf(3)-style
 * conversion specifications will be copied verbatim, including "%%", even if
 * followed by an "m".  Returns a static buffer in order to not fail on memory
 * allocation error.
 */
static char *
insert_syserr(const char *buf)
{
	static char newbuf[8192];
	char *errstr = strerror(errno);
	size_t errlen = strlen(errstr);
	size_t copylen;
	unsigned i, j;

	for (i = 0, j = 0; buf[i] != '\0' && j < sizeof(newbuf) - 2; i++, j++) {
		if (buf[i] == '%') {
			if (buf[++i] == 'm') {
				copylen = (errlen > sizeof(newbuf) - j - 1) ?
				    sizeof(newbuf) - j - 1 : errlen;
				memcpy(newbuf + j, errstr, copylen);
				/*
				 * As we'll increment j by 1 after the iteration
				 * anyway, we only increment j by the number of
				 * copied bytes - 1.
				 */
				j += copylen - 1;
				continue;
			} else {
				/*
				 * The possibility to run into this block is the
				 * reason we checked for j < sizeof(newbuf) - 2
				 * instead of j < sizeof(newbuf) - 1.
				 */
				newbuf[j++] = '%';
				if (buf[i] == '\0')
					break;
			}
		}
		newbuf[j] = buf[i];
	}
	newbuf[j] = '\0';
	return newbuf;
}
