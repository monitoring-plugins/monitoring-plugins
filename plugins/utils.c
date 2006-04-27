/*****************************************************************************
 *
 * utils.c
 *
 * Library of useful functions for plugins
 *
 * Copyright (c) 2000 Karl DeBisschop (karl@debisschop.net)
 * Copyright (c) 2006 Nagios Plugin Development Team
 * License: GPL
 *
 * $Revision$
 * $Date$
 ****************************************************************************/

#define LOCAL_TIMEOUT_ALARM_HANDLER

#include "common.h"
#include "utils.h"
#include <stdarg.h>
#include <limits.h>

#include <arpa/inet.h>

extern void print_usage (void);
extern const char *progname;

#define STRLEN 64
#define TXTBLK 128

/* **************************************************************************
 * max_state(STATE_x, STATE_y)
 * compares STATE_x to  STATE_y and returns result based on the following
 * STATE_UNKNOWN < STATE_OK < STATE_WARNING < STATE_CRITICAL
 *
 * Note that numerically the above does not hold
 ****************************************************************************/

int
max_state (int a, int b)
{
	if (a == STATE_CRITICAL || b == STATE_CRITICAL)
		return STATE_CRITICAL;
	else if (a == STATE_WARNING || b == STATE_WARNING)
		return STATE_WARNING;
	else if (a == STATE_OK || b == STATE_OK)
		return STATE_OK;
	else if (a == STATE_UNKNOWN || b == STATE_UNKNOWN)
		return STATE_UNKNOWN;
	else if (a == STATE_DEPENDENT || b == STATE_DEPENDENT)
		return STATE_DEPENDENT;
	else
		return max (a, b);
}

void usage (const char *msg)
{
	printf ("%s\n", msg);
	print_usage ();
	exit (STATE_UNKNOWN);
}

void usage_va (const char *fmt, ...)
{
	va_list ap;
	printf("%s: ", progname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	exit (STATE_UNKNOWN);
}

void usage2(const char *msg, const char *arg)
{
	printf ("%s: %s - %s\n", progname, msg, arg?arg:"(null)" );
	print_usage ();
	exit (STATE_UNKNOWN);
}

void
usage3 (const char *msg, int arg)
{
	printf ("%s: %s - %c\n", progname, msg, arg);
	print_usage();
	exit (STATE_UNKNOWN);
}

void
usage4 (const char *msg)
{
	printf ("%s: %s\n", progname, msg);
	print_usage();
	exit (STATE_UNKNOWN);
}

char *
clean_revstring (const char *revstring)
{
	char plugin_revision[STRLEN];
	if (sscanf (revstring,"$Revision: %[0-9.]",plugin_revision) == 1)
		return strscpy (NULL, plugin_revision);
	else
	  return strscpy (NULL, "N/A");
}

void
print_revision (const char *command_name, const char *revision_string)
{
	char plugin_revision[STRLEN];

	if (sscanf (revision_string, "$Revision: %[0-9.]", plugin_revision) != 1)
		strncpy (plugin_revision, "N/A", STRLEN);
	printf ("%s (%s %s) %s\n",
					command_name, PACKAGE, VERSION, plugin_revision);
}

const char *
state_text (int result)
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

void
die (int result, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vprintf (fmt, ap);
	va_end (ap);
	exit (result);
}

void
timeout_alarm_handler (int signo)
{
	if (signo == SIGALRM) {
		printf (_("CRITICAL - Plugin timed out after %d seconds\n"),
						timeout_interval);
		exit (STATE_CRITICAL);
	}
}

int
is_numeric (char *number)
{
	char tmp[1];
	float x;

	if (!number)
		return FALSE;
	else if (sscanf (number, "%f%c", &x, tmp) == 1)
		return TRUE;
	else
		return FALSE;
}

int
is_positive (char *number)
{
	if (is_numeric (number) && atof (number) > 0.0)
		return TRUE;
	else
		return FALSE;
}

int
is_negative (char *number)
{
	if (is_numeric (number) && atof (number) < 0.0)
		return TRUE;
	else
		return FALSE;
}

int
is_nonnegative (char *number)
{
	if (is_numeric (number) && atof (number) >= 0.0)
		return TRUE;
	else
		return FALSE;
}

int
is_percentage (char *number)
{
	int x;
	if (is_numeric (number) && (x = atof (number)) >= 0 && x <= 100)
		return TRUE;
	else
		return FALSE;
}

int
is_integer (char *number)
{
	long int n;

	if (!number || (strspn (number, "-0123456789 ") != strlen (number)))
		return FALSE;

	n = strtol (number, NULL, 10);

	if (errno != ERANGE && n >= INT_MIN && n <= INT_MAX)
		return TRUE;
	else
		return FALSE;
}

int
is_intpos (char *number)
{
	if (is_integer (number) && atoi (number) > 0)
		return TRUE;
	else
		return FALSE;
}

int
is_intneg (char *number)
{
	if (is_integer (number) && atoi (number) < 0)
		return TRUE;
	else
		return FALSE;
}

int
is_intnonneg (char *number)
{
	if (is_integer (number) && atoi (number) >= 0)
		return TRUE;
	else
		return FALSE;
}

int
is_intpercent (char *number)
{
	int i;
	if (is_integer (number) && (i = atoi (number)) >= 0 && i <= 100)
		return TRUE;
	else
		return FALSE;
}

int
is_option (char *str)
{
	if (!str)
		return FALSE;
	else if (strspn (str, "-") == 1 || strspn (str, "-") == 2)
		return TRUE;
	else
		return FALSE;
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
			return 1;
		}
	}
	if (critical_string != NULL) {
		if ((temp_thresholds->critical = parse_range_string(critical_string)) == NULL) {
			return 1;
		}
	}

	if (*my_thresholds != 0) {
		/* printf("Freeing here: %d\n", *my_thresholds); */
		free(*my_thresholds);
	}
	*my_thresholds = temp_thresholds;

	return 0;
}

void
set_thresholds(thresholds **my_thresholds, char *warn_string, char *critical_string)
{
	if (_set_thresholds(my_thresholds, warn_string, critical_string) == 0) {
		return;
	} else {
		usage("Range format incorrect");
	}
}

/* Returns TRUE if alert should be raised based on the range */
int
check_range(double value, range *my_range)
{
	int false = FALSE;
	int true = TRUE;
	
	if (my_range->alert_on == INSIDE) {
		false = TRUE;
		true = FALSE;
	}

	if (my_range->end_infinity == FALSE && my_range->start_infinity == FALSE) {
		if ((my_range->start <= value) && (value <= my_range->end)) {
			return false;
		} else {
			return true;
		}
	} else if (my_range->start_infinity == FALSE && my_range->end_infinity == TRUE) {
		if (my_range->start <= value) {
			return false;
		} else {
			return true;
		}
	} else if (my_range->start_infinity == TRUE && my_range->end_infinity == FALSE) {
		if (value <= my_range->end) {
			return false;
		} else {
			return true;
		}
	} else {
		return false;
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

#ifdef NEED_GETTIMEOFDAY
int
gettimeofday (struct timeval *tv, struct timezone *tz)
{
	tv->tv_usec = 0;
	tv->tv_sec = (long) time ((time_t) 0);
}
#endif



double
delta_time (struct timeval tv)
{
	struct timeval now;

	gettimeofday (&now, NULL);
	return ((double)(now.tv_sec - tv.tv_sec) + (double)(now.tv_usec - tv.tv_usec) / (double)1000000);
}



long
deltime (struct timeval tv)
{
	struct timeval now;
	gettimeofday (&now, NULL);
	return (now.tv_sec - tv.tv_sec)*1000000 + now.tv_usec - tv.tv_usec;
}




void
strip (char *buffer)
{
	size_t x;
	int i;

	for (x = strlen (buffer); x >= 1; x--) {
		i = x - 1;
		if (buffer[i] == ' ' ||
				buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == '\t')
			buffer[i] = '\0';
		else
			break;
	}
	return;
}


/******************************************************************************
 *
 * Copies one string to another. Any previously existing data in
 * the destination string is lost.
 *
 * Example:
 *
 * char *str=NULL;
 * str = strscpy("This is a line of text with no trailing newline");
 *
 *****************************************************************************/

char *
strscpy (char *dest, const char *src)
{
	if (src == NULL)
		return NULL;

	asprintf (&dest, "%s", src);

	return dest;
}



/******************************************************************************
 *
 * Returns a pointer to the next line of a multiline string buffer
 *
 * Given a pointer string, find the text following the next sequence
 * of \r and \n characters. This has the effect of skipping blank
 * lines as well
 *
 * Example:
 *
 * Given text as follows:
 *
 * ==============================
 * This
 * is
 * a
 * 
 * multiline string buffer
 * ==============================
 *
 * int i=0;
 * char *str=NULL;
 * char *ptr=NULL;
 * str = strscpy(str,"This\nis\r\na\n\nmultiline string buffer\n");
 * ptr = str;
 * while (ptr) {
 *   printf("%d %s",i++,firstword(ptr));
 *   ptr = strnl(ptr);
 * }
 * 
 * Produces the following:
 *
 * 1 This
 * 2 is
 * 3 a
 * 4 multiline
 *
 * NOTE: The 'firstword()' function is conceptual only and does not
 *       exist in this package.
 *
 * NOTE: Although the second 'ptr' variable is not strictly needed in
 *       this example, it is good practice with these utilities. Once
 *       the * pointer is advance in this manner, it may no longer be
 *       handled with * realloc(). So at the end of the code fragment
 *       above, * strscpy(str,"foo") work perfectly fine, but
 *       strscpy(ptr,"foo") will * cause the the program to crash with
 *       a segmentation fault.
 *
 *****************************************************************************/

char *
strnl (char *str)
{
	size_t len;
	if (str == NULL)
		return NULL;
	str = strpbrk (str, "\r\n");
	if (str == NULL)
		return NULL;
	len = strspn (str, "\r\n");
	if (str[len] == '\0')
		return NULL;
	str += len;
	if (strlen (str) == 0)
		return NULL;
	return str;
}


/******************************************************************************
 *
 * Like strscpy, except only the portion of the source string up to
 * the provided delimiter is copied.
 *
 * Example:
 *
 * str = strpcpy(str,"This is a line of text with no trailing newline","x");
 * printf("%s\n",str);
 *
 * Produces:
 *
 *This is a line of te
 *
 *****************************************************************************/

char *
strpcpy (char *dest, const char *src, const char *str)
{
	size_t len;

	if (src)
		len = strcspn (src, str);
	else
		return NULL;

	if (dest == NULL || strlen (dest) < len)
		dest = realloc (dest, len + 1);
	if (dest == NULL)
		die (STATE_UNKNOWN, _("failed realloc in strpcpy\n"));

	strncpy (dest, src, len);
	dest[len] = '\0';

	return dest;
}



/******************************************************************************
 *
 * Like strscat, except only the portion of the source string up to
 * the provided delimiter is copied.
 *
 * str = strpcpy(str,"This is a line of text with no trailing newline","x");
 * str = strpcat(str,"This is a line of text with no trailing newline","x");
 * printf("%s\n",str);
 * 
 *This is a line of texThis is a line of tex
 *
 *****************************************************************************/

char *
strpcat (char *dest, const char *src, const char *str)
{
	size_t len, l2;

	if (dest)
		len = strlen (dest);
	else
		len = 0;

	if (src) {
		l2 = strcspn (src, str);
	}
	else {
		return dest;
	}

	dest = realloc (dest, len + l2 + 1);
	if (dest == NULL)
		die (STATE_UNKNOWN, _("failed malloc in strscat\n"));

	strncpy (dest + len, src, l2);
	dest[len + l2] = '\0';

	return dest;
}

#ifndef HAVE_BASENAME
/* function modified from coreutils base_name function - see ACKNOWLEDGEMENTS */
char *basename(const char *path) {
	char const *base = path;
	char const *p;
	for (p = base; *p; p++) {
		if (*p == '/') {
			/* Treat multiple adjacent slashes like single slash */
			do p++;
			while (*p == '/');

			/* If filename ends in slash, use trailing slash
			   as basename if no non-slashes found */
			if (! *p) {
				if (*base == '/')
					base = p - 1;
				break;
			}

			/* *p is non-slash preceded by slash */
			base = p;
		}
	}
	return (char *) base;
}
#endif

/******************************************************************************
 *
 * Print perfdata in a standard format
 *
 ******************************************************************************/

char *perfdata (const char *label,
 long int val,
 const char *uom,
 int warnp,
 long int warn,
 int critp,
 long int crit,
 int minp,
 long int minv,
 int maxp,
 long int maxv)
{
	char *data = NULL;

	if (strpbrk (label, "'= "))
		asprintf (&data, "'%s'=%ld%s;", label, val, uom);
	else
		asprintf (&data, "%s=%ld%s;", label, val, uom);

	if (warnp)
		asprintf (&data, "%s%ld;", data, warn);
	else
		asprintf (&data, "%s;", data);

	if (critp)
		asprintf (&data, "%s%ld;", data, crit);
	else
		asprintf (&data, "%s;", data);

	if (minp)
		asprintf (&data, "%s%ld", data, minv);

	if (maxp)
		asprintf (&data, "%s;%ld", data, maxv);

	return data;
}


char *fperfdata (const char *label,
 double val,
 const char *uom,
 int warnp,
 double warn,
 int critp,
 double crit,
 int minp,
 double minv,
 int maxp,
 double maxv)
{
	char *data = NULL;

	if (strpbrk (label, "'= "))
		asprintf (&data, "'%s'=", label);
	else
		asprintf (&data, "%s=", label);

	asprintf (&data, "%s%f", data, val);
	asprintf (&data, "%s%s;", data, uom);

	if (warnp)
		asprintf (&data, "%s%f", data, warn);

	asprintf (&data, "%s;", data);

	if (critp)
		asprintf (&data, "%s%f", data, crit);

	asprintf (&data, "%s;", data);

	if (minp)
		asprintf (&data, "%s%f", data, minv);

	if (maxp) {
		asprintf (&data, "%s;", data);
		asprintf (&data, "%s%f", data, maxv);
	}

	return data;
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
