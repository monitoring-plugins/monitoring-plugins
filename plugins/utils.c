/*****************************************************************************
* 
* Library of useful functions for plugins
* 
* License: GPL
* Copyright (c) 2000 Karl DeBisschop (karl@debisschop.net)
* Copyright (c) 2002-2007 Monitoring Plugins Development Team
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
#include "utils.h"
#include "../lib/utils_base.h"
#include <stdarg.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include <arpa/inet.h>


extern void print_usage (void);
extern const char *progname;

#define STRLEN 64
#define TXTBLK 128

time_t start_time, end_time;

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

void
usage5 (void)
{
	print_usage();
	exit (STATE_UNKNOWN);
}

void
print_revision (const char *command_name, const char *revision)
{
	printf ("%s v%s (%s %s)\n",
	         command_name, revision, PACKAGE, VERSION);
}

bool is_numeric (char *number) {
	char tmp[1];
	float x;

	if (!number)
		return false;
	else if (sscanf (number, "%f%c", &x, tmp) == 1)
		return true;
	else
		return false;
}

bool is_positive (char *number) {
	if (is_numeric (number) && atof (number) > 0.0)
		return true;
	else
		return false;
}

bool is_negative (char *number) {
	if (is_numeric (number) && atof (number) < 0.0)
		return true;
	else
		return false;
}

bool is_nonnegative (char *number) {
	if (is_numeric (number) && atof (number) >= 0.0)
		return true;
	else
		return false;
}

bool is_percentage (char *number) {
	int x;
	if (is_numeric (number) && (x = atof (number)) >= 0 && x <= 100)
		return true;
	else
		return false;
}

bool is_intpos (char *number) {
	if (is_integer (number) && atoi (number) > 0)
		return true;
	else
		return false;
}

bool is_intneg (char *number) {
	if (is_integer (number) && atoi (number) < 0)
		return true;
	else
		return false;
}

bool is_intnonneg (char *number) {
	if (is_integer (number) && atoi (number) >= 0)
		return true;
	else
		return false;
}

/*
 * Checks whether the number in the string _number_ can be put inside a int64_t
 * On success the number will be written to the _target_ address, if _target_ is not set
 * to NULL.
 */
bool is_int64(char *number, int64_t *target) {
	errno = 0;
	char *endptr = { 0 };

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
	char *endptr = { 0 };
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

bool is_intpercent (char *number) {
	int i;
	if (is_integer (number) && (i = atoi (number)) >= 0 && i <= 100)
		return true;
	else
		return false;
}

bool is_option (char *str) {
	if (!str)
		return false;
	else if (strspn (str, "-") == 1 || strspn (str, "-") == 2)
		return true;
	else
		return false;
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
		xasprintf (&data, "'%s'=%ld%s;", label, val, uom);
	else
		xasprintf (&data, "%s=%ld%s;", label, val, uom);

	if (warnp)
		xasprintf (&data, "%s%ld;", data, warn);
	else
		xasprintf (&data, "%s;", data);

	if (critp)
		xasprintf (&data, "%s%ld;", data, crit);
	else
		xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%ld;", data, minv);
	else
		xasprintf (&data, "%s;", data);

	if (maxp)
		xasprintf (&data, "%s%ld", data, maxv);

	return data;
}


char *perfdata_uint64 (const char *label,
 uint64_t val,
 const char *uom,
 int warnp, /* Warning present */
 uint64_t warn,
 int critp, /* Critical present */
 uint64_t crit,
 int minp, /* Minimum present */
 uint64_t minv,
 int maxp, /* Maximum present */
 uint64_t maxv)
{
	char *data = NULL;

	if (strpbrk (label, "'= "))
		xasprintf (&data, "'%s'=%" PRIu64 "%s;", label, val, uom);
	else
		xasprintf (&data, "%s=%" PRIu64 "%s;", label, val, uom);

	if (warnp)
		xasprintf (&data, "%s%" PRIu64 ";", data, warn);
	else
		xasprintf (&data, "%s;", data);

	if (critp)
		xasprintf (&data, "%s%" PRIu64 ";", data, crit);
	else
		xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%" PRIu64 ";", data, minv);
	else
		xasprintf (&data, "%s;", data);

	if (maxp)
		xasprintf (&data, "%s%" PRIu64, data, maxv);

	return data;
}


char *perfdata_int64 (const char *label,
 int64_t val,
 const char *uom,
 int warnp, /* Warning present */
 int64_t warn,
 int critp, /* Critical present */
 int64_t crit,
 int minp, /* Minimum present */
 int64_t minv,
 int maxp, /* Maximum present */
 int64_t maxv)
{
	char *data = NULL;

	if (strpbrk (label, "'= "))
		xasprintf (&data, "'%s'=%" PRId64 "%s;", label, val, uom);
	else
		xasprintf (&data, "%s=%" PRId64 "%s;", label, val, uom);

	if (warnp)
		xasprintf (&data, "%s%" PRId64 ";", data, warn);
	else
		xasprintf (&data, "%s;", data);

	if (critp)
		xasprintf (&data, "%s%" PRId64 ";", data, crit);
	else
		xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%" PRId64 ";", data, minv);
	else
		xasprintf (&data, "%s;", data);

	if (maxp)
		xasprintf (&data, "%s%" PRId64, data, maxv);

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
		xasprintf (&data, "'%s'=", label);
	else
		xasprintf (&data, "%s=", label);

	xasprintf (&data, "%s%f", data, val);
	xasprintf (&data, "%s%s;", data, uom);

	if (warnp)
		xasprintf (&data, "%s%f", data, warn);

	xasprintf (&data, "%s;", data);

	if (critp)
		xasprintf (&data, "%s%f", data, crit);

	xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%f", data, minv);

	if (maxp) {
		xasprintf (&data, "%s;", data);
		xasprintf (&data, "%s%f", data, maxv);
	}

	return data;
}

char *sperfdata (const char *label,
 double val,
 const char *uom,
 char *warn,
 char *crit,
 int minp,
 double minv,
 int maxp,
 double maxv)
{
	char *data = NULL;
	if (strpbrk (label, "'= "))
		xasprintf (&data, "'%s'=", label);
	else
		xasprintf (&data, "%s=", label);

	xasprintf (&data, "%s%f", data, val);
	xasprintf (&data, "%s%s;", data, uom);

	if (warn!=NULL)
		xasprintf (&data, "%s%s", data, warn);

	xasprintf (&data, "%s;", data);

	if (crit!=NULL)
		xasprintf (&data, "%s%s", data, crit);

	xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%f", data, minv);

	if (maxp) {
		xasprintf (&data, "%s;", data);
		xasprintf (&data, "%s%f", data, maxv);
	}

	return data;
}

char *sperfdata_int (const char *label,
 int val,
 const char *uom,
 char *warn,
 char *crit,
 int minp,
 int minv,
 int maxp,
 int maxv)
{
	char *data = NULL;
	if (strpbrk (label, "'= "))
		xasprintf (&data, "'%s'=", label);
	else
		xasprintf (&data, "%s=", label);

	xasprintf (&data, "%s%d", data, val);
	xasprintf (&data, "%s%s;", data, uom);

	if (warn!=NULL)
		xasprintf (&data, "%s%s", data, warn);

	xasprintf (&data, "%s;", data);

	if (crit!=NULL)
		xasprintf (&data, "%s%s", data, crit);

	xasprintf (&data, "%s;", data);

	if (minp)
		xasprintf (&data, "%s%d", data, minv);

	if (maxp) {
		xasprintf (&data, "%s;", data);
		xasprintf (&data, "%s%d", data, maxv);
	}

	return data;
}
