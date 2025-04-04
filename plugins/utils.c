/*****************************************************************************
 *
 * Library of useful functions for plugins
 *
 * License: GPL
 * Copyright (c) 2000 Karl DeBisschop (karl@debisschop.net)
 * Copyright (c) 2002-2024 Monitoring Plugins Development Team
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
#include "./utils.h"
#include "utils_base.h"
#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <stdbool.h>

#include <arpa/inet.h>

extern void print_usage(void);
extern const char *progname;

#define STRLEN 64
#define TXTBLK 128

time_t start_time, end_time;

void usage(const char *msg) {
	printf("%s\n", msg);
	print_usage();
	exit(STATE_UNKNOWN);
}

void usage_va(const char *fmt, ...) {
	va_list ap;
	printf("%s: ", progname);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	exit(STATE_UNKNOWN);
}

void usage2(const char *msg, const char *arg) {
	printf("%s: %s - %s\n", progname, msg, arg ? arg : "(null)");
	print_usage();
	exit(STATE_UNKNOWN);
}

void usage3(const char *msg, int arg) {
	printf("%s: %s - %c\n", progname, msg, arg);
	print_usage();
	exit(STATE_UNKNOWN);
}

void usage4(const char *msg) {
	printf("%s: %s\n", progname, msg);
	print_usage();
	exit(STATE_UNKNOWN);
}

void usage5(void) {
	print_usage();
	exit(STATE_UNKNOWN);
}

void print_revision(const char *command_name, const char *revision) {
	printf("%s v%s (%s %s)\n", command_name, revision, PACKAGE, VERSION);
}

bool is_numeric(char *number) {
	char tmp[1];
	float x;

	if (!number) {
		return false;
	} else if (sscanf(number, "%f%c", &x, tmp) == 1) {
		return true;
	} else {
		return false;
	}
}

bool is_positive(char *number) {
	if (is_numeric(number) && atof(number) > 0.0) {
		return true;
	} else {
		return false;
	}
}

bool is_negative(char *number) {
	if (is_numeric(number) && atof(number) < 0.0) {
		return true;
	} else {
		return false;
	}
}

bool is_nonnegative(char *number) {
	if (is_numeric(number) && atof(number) >= 0.0) {
		return true;
	} else {
		return false;
	}
}

bool is_percentage(char *number) {
	int x;
	if (is_numeric(number) && (x = atof(number)) >= 0 && x <= 100) {
		return true;
	} else {
		return false;
	}
}

bool is_percentage_expression(const char str[]) {
	if (!str) {
		return false;
	}

	size_t len = strlen(str);

	if (str[len - 1] != '%') {
		return false;
	}

	char *foo = calloc(len + 1, sizeof(char));

	if (!foo) {
		die(STATE_UNKNOWN, _("calloc failed \n"));
	}

	strcpy(foo, str);
	foo[len - 1] = '\0';

	bool result = is_numeric(foo);

	free(foo);

	return result;
}

bool is_integer(char *number) {
	long int n;

	if (!number || (strspn(number, "-0123456789 ") != strlen(number))) {
		return false;
	}

	n = strtol(number, NULL, 10);

	if (errno != ERANGE && n >= INT_MIN && n <= INT_MAX) {
		return true;
	} else {
		return false;
	}
}

bool is_intpos(char *number) {
	if (is_integer(number) && atoi(number) > 0) {
		return true;
	} else {
		return false;
	}
}

bool is_intneg(char *number) {
	if (is_integer(number) && atoi(number) < 0) {
		return true;
	} else {
		return false;
	}
}

bool is_intnonneg(char *number) {
	if (is_integer(number) && atoi(number) >= 0) {
		return true;
	} else {
		return false;
	}
}

/*
 * Checks whether the number in the string _number_ can be put inside a int64_t
 * On success the number will be written to the _target_ address, if _target_ is not set
 * to NULL.
 */
bool is_int64(char *number, int64_t *target) {
	errno = 0;
	char *endptr = {0};

	int64_t tmp = strtoll(number, &endptr, 10);
	if (errno != 0) {
		return false;
	}

	if (*endptr == '\0') {
		return 0;
	}

	if (tmp < INT64_MIN || tmp > INT64_MAX) {
		return false;
	}

	if (target != NULL) {
		*target = tmp;
	}
	return true;
}

/*
 * Checks whether the number in the string _number_ can be put inside a uint64_t
 * On success the number will be written to the _target_ address, if _target_ is not set
 * to NULL.
 */
bool is_uint64(char *number, uint64_t *target) {
	errno = 0;
	char *endptr = {0};
	unsigned long long tmp = strtoull(number, &endptr, 10);

	if (errno != 0) {
		return false;
	}

	if (*endptr != '\0') {
		return false;
	}

	if (tmp > UINT64_MAX) {
		return false;
	}

	if (target != NULL) {
		*target = (uint64_t)tmp;
	}

	return true;
}

bool is_intpercent(char *number) {
	int i;
	if (is_integer(number) && (i = atoi(number)) >= 0 && i <= 100) {
		return true;
	} else {
		return false;
	}
}

bool is_option(char *str) {
	if (!str) {
		return false;
	} else if (strspn(str, "-") == 1 || strspn(str, "-") == 2) {
		return true;
	} else {
		return false;
	}
}

#ifdef NEED_GETTIMEOFDAY
int gettimeofday(struct timeval *tv, struct timezone *tz) {
	tv->tv_usec = 0;
	tv->tv_sec = (long)time((time_t)0);
}
#endif

double delta_time(struct timeval tv) {
	struct timeval now;

	gettimeofday(&now, NULL);
	return ((double)(now.tv_sec - tv.tv_sec) + (double)(now.tv_usec - tv.tv_usec) / (double)1000000);
}

long deltime(struct timeval tv) {
	struct timeval now;
	gettimeofday(&now, NULL);
	return (now.tv_sec - tv.tv_sec) * 1000000 + now.tv_usec - tv.tv_usec;
}

void strip(char *buffer) {
	size_t x;
	int i;

	for (x = strlen(buffer); x >= 1; x--) {
		i = x - 1;
		if (buffer[i] == ' ' || buffer[i] == '\r' || buffer[i] == '\n' || buffer[i] == '\t') {
			buffer[i] = '\0';
		} else {
			break;
		}
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

char *strscpy(char *dest, const char *src) {
	if (src == NULL) {
		return NULL;
	}

	xasprintf(&dest, "%s", src);

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

char *strnl(char *str) {
	size_t len;
	if (str == NULL) {
		return NULL;
	}
	str = strpbrk(str, "\r\n");
	if (str == NULL) {
		return NULL;
	}
	len = strspn(str, "\r\n");
	if (str[len] == '\0') {
		return NULL;
	}
	str += len;
	if (strlen(str) == 0) {
		return NULL;
	}
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

char *strpcpy(char *dest, const char *src, const char *str) {
	size_t len;

	if (src) {
		len = strcspn(src, str);
	} else {
		return NULL;
	}

	if (dest == NULL || strlen(dest) < len) {
		dest = realloc(dest, len + 1);
	}
	if (dest == NULL) {
		die(STATE_UNKNOWN, _("failed realloc in strpcpy\n"));
	}

	strncpy(dest, src, len);
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

char *strpcat(char *dest, const char *src, const char *str) {
	size_t len, l2;

	if (dest) {
		len = strlen(dest);
	} else {
		len = 0;
	}

	if (src) {
		l2 = strcspn(src, str);
	} else {
		return dest;
	}

	dest = realloc(dest, len + l2 + 1);
	if (dest == NULL) {
		die(STATE_UNKNOWN, _("failed malloc in strscat\n"));
	}

	strncpy(dest + len, src, l2);
	dest[len + l2] = '\0';

	return dest;
}

/******************************************************************************
 *
 * asprintf, but die on failure
 *
 ******************************************************************************/

int xvasprintf(char **strp, const char *fmt, va_list ap) {
	int result = vasprintf(strp, fmt, ap);
	if (result == -1 || *strp == NULL) {
		die(STATE_UNKNOWN, _("failed malloc in xvasprintf\n"));
	}
	return result;
}

int xasprintf(char **strp, const char *fmt, ...) {
	va_list ap;
	int result;
	va_start(ap, fmt);
	result = xvasprintf(strp, fmt, ap);
	va_end(ap);
	return result;
}

/******************************************************************************
 *
 * Print perfdata in a standard format
 *
 ******************************************************************************/

char *perfdata(const char *label, long int val, const char *uom, bool warnp, long int warn, bool critp, long int crit, bool minp,
			   long int minv, bool maxp, long int maxv) {
	char *data = NULL;

	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=%ld%s;", label, val, uom);
	} else {
		xasprintf(&data, "%s=%ld%s;", label, val, uom);
	}

	if (warnp) {
		xasprintf(&data, "%s%ld;", data, warn);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (critp) {
		xasprintf(&data, "%s%ld;", data, crit);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (minp) {
		xasprintf(&data, "%s%ld;", data, minv);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (maxp) {
		xasprintf(&data, "%s%ld", data, maxv);
	}

	return data;
}

char *perfdata_uint64(const char *label, uint64_t val, const char *uom, bool warnp, /* Warning present */
					  uint64_t warn, bool critp,                                    /* Critical present */
					  uint64_t crit, bool minp,                                     /* Minimum present */
					  uint64_t minv, bool maxp,                                     /* Maximum present */
					  uint64_t maxv) {
	char *data = NULL;

	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=%" PRIu64 "%s;", label, val, uom);
	} else {
		xasprintf(&data, "%s=%" PRIu64 "%s;", label, val, uom);
	}

	if (warnp) {
		xasprintf(&data, "%s%" PRIu64 ";", data, warn);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (critp) {
		xasprintf(&data, "%s%" PRIu64 ";", data, crit);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (minp) {
		xasprintf(&data, "%s%" PRIu64 ";", data, minv);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (maxp) {
		xasprintf(&data, "%s%" PRIu64, data, maxv);
	}

	return data;
}

char *perfdata_int64(const char *label, int64_t val, const char *uom, bool warnp, /* Warning present */
					 int64_t warn, bool critp,                                    /* Critical present */
					 int64_t crit, bool minp,                                     /* Minimum present */
					 int64_t minv, bool maxp,                                     /* Maximum present */
					 int64_t maxv) {
	char *data = NULL;

	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=%" PRId64 "%s;", label, val, uom);
	} else {
		xasprintf(&data, "%s=%" PRId64 "%s;", label, val, uom);
	}

	if (warnp) {
		xasprintf(&data, "%s%" PRId64 ";", data, warn);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (critp) {
		xasprintf(&data, "%s%" PRId64 ";", data, crit);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (minp) {
		xasprintf(&data, "%s%" PRId64 ";", data, minv);
	} else {
		xasprintf(&data, "%s;", data);
	}

	if (maxp) {
		xasprintf(&data, "%s%" PRId64, data, maxv);
	}

	return data;
}

char *fperfdata(const char *label, double val, const char *uom, bool warnp, double warn, bool critp, double crit, bool minp, double minv,
				bool maxp, double maxv) {
	char *data = NULL;

	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=", label);
	} else {
		xasprintf(&data, "%s=", label);
	}

	xasprintf(&data, "%s%f", data, val);
	xasprintf(&data, "%s%s;", data, uom);

	if (warnp) {
		xasprintf(&data, "%s%f", data, warn);
	}

	xasprintf(&data, "%s;", data);

	if (critp) {
		xasprintf(&data, "%s%f", data, crit);
	}

	xasprintf(&data, "%s;", data);

	if (minp) {
		xasprintf(&data, "%s%f", data, minv);
	}

	if (maxp) {
		xasprintf(&data, "%s;", data);
		xasprintf(&data, "%s%f", data, maxv);
	}

	return data;
}

char *sperfdata(const char *label, double val, const char *uom, char *warn, char *crit, bool minp, double minv, bool maxp, double maxv) {
	char *data = NULL;
	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=", label);
	} else {
		xasprintf(&data, "%s=", label);
	}

	xasprintf(&data, "%s%f", data, val);
	xasprintf(&data, "%s%s;", data, uom);

	if (warn != NULL) {
		xasprintf(&data, "%s%s", data, warn);
	}

	xasprintf(&data, "%s;", data);

	if (crit != NULL) {
		xasprintf(&data, "%s%s", data, crit);
	}

	xasprintf(&data, "%s;", data);

	if (minp) {
		xasprintf(&data, "%s%f", data, minv);
	}

	if (maxp) {
		xasprintf(&data, "%s;", data);
		xasprintf(&data, "%s%f", data, maxv);
	}

	return data;
}

char *sperfdata_int(const char *label, int val, const char *uom, char *warn, char *crit, bool minp, int minv, bool maxp, int maxv) {
	char *data = NULL;
	if (strpbrk(label, "'= ")) {
		xasprintf(&data, "'%s'=", label);
	} else {
		xasprintf(&data, "%s=", label);
	}

	xasprintf(&data, "%s%d", data, val);
	xasprintf(&data, "%s%s;", data, uom);

	if (warn != NULL) {
		xasprintf(&data, "%s%s", data, warn);
	}

	xasprintf(&data, "%s;", data);

	if (crit != NULL) {
		xasprintf(&data, "%s%s", data, crit);
	}

	xasprintf(&data, "%s;", data);

	if (minp) {
		xasprintf(&data, "%s%d", data, minv);
	}

	if (maxp) {
		xasprintf(&data, "%s;", data);
		xasprintf(&data, "%s%d", data, maxv);
	}

	return data;
}
