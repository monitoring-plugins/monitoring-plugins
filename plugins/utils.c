/*****************************************************************************
 *
 * utils.c
 *
 * Library of useful functions for plugins
 *
 * Copyright (c) 2000 Karl DeBisschop (karl@debisschop.net)
 * License: GPL
 *
 * $Revision$
 * $Date$
 ****************************************************************************/

#include "config.h"
#include "common.h"
#include <stdarg.h>
#include <limits.h>

#include <arpa/inet.h>

extern int timeout_interval;
extern const char *progname;

void support (void);
char *clean_revstring (const char *);
void print_revision (const char *, const char *);
void die (int result, const char *fmt, ...);
void terminate (int result, const char *fmt, ...);
RETSIGTYPE timeout_alarm_handler (int);

int is_integer (char *);
int is_intpos (char *);
int is_intneg (char *);
int is_intnonneg (char *);
int is_intpercent (char *);

int is_numeric (char *);
int is_positive (char *);
int is_negative (char *);
int is_nonnegative (char *);
int is_percentage (char *);

int is_option (char *str);

double delta_time (struct timeval tv);

void strip (char *);
char *strscpy (char *dest, const char *src);
char *strscat (char *dest, char *src);
char *strnl (char *str);
char *strpcpy (char *dest, const char *src, const char *str);
char *strpcat (char *dest, const char *src, const char *str);

char *state_text (int result);

#define LABELLEN 63
#define STRLEN 64
#define TXTBLK 128

/* **************************************************************************
 /* max_state(STATE_x, STATE_y)
 * compares STATE_x to  STATE_y and returns result based on the following
 * STATE_UNKNOWN < STATE_OK < STATE_WARNING < STATE_CRITICAL
 *
 * Note that numerically the above does not hold
 ****************************************************************************/

#define max(a,b) (((a)>(b))?(a):(b))

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

void usage (char *msg)
{
	printf (msg);
	print_usage ();
	exit (STATE_UNKNOWN);
}

void usage2(char *msg, char *arg)
{
	printf ("%s: %s - %s\n",progname,msg,arg);
	print_usage ();
	exit (STATE_UNKNOWN);
}

void
usage3 (char *msg, char arg)
{
	printf ("%s: %s - %c\n", progname, msg, arg);
	print_usage();
	exit (STATE_UNKNOWN);
}


void
support (void)
{
	printf
		("Send email to nagios-users@lists.sourceforge.net if you have questions\n"
		 "regarding use of this software. To submit patches or suggest improvements,\n"
		 "send email to nagiosplug-devel@lists.sourceforge.net\n");
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
	printf ("%s (nagios-plugins %s) %s\n",
					progname, VERSION, plugin_revision);
	printf
		("The nagios plugins come with ABSOLUTELY NO WARRANTY. You may redistribute\n"
		 "copies of the plugins under the terms of the GNU General Public License.\n"
		 "For more information about these matters, see the file named COPYING.\n");

}

char *
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
	printf ("%s %s: ", sizeof (char) + index(progname, '_'), state_text(result));
	va_start (ap, fmt);
	vprintf (fmt, ap);
	va_end (ap);
	exit (result);
}

void
terminate (int result, const char *fmt, ...)
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
		printf ("CRITICAL - Plugin timed out after %d seconds\n",
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
 * Concatenates one string to the end of another
 *
 * Given a pointer destination string, which may or may not already
 * hold some text, and a source string with additional text (possibly
 * NULL or empty), returns a pointer to a string that is the first
 * string with the second concatenated to it. Uses realloc to free 
 * memory held by the dest argument if new storage space is required.
 *
 * Example:
 *
 * char *str=NULL;
 * str = strscpy("This is a line of text with no trailing newline");
 * str = strscat(str,"\n");
 *
 *****************************************************************************/

char *
strscat (char *dest, char *src)
{

	if (dest == NULL)
		return src;
	if (src != NULL)
		asprintf (&dest, "%s%s", dest, src);

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
		terminate (STATE_UNKNOWN, "failed realloc in strpcpy\n");

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
		terminate (STATE_UNKNOWN, "failed malloc in strscat\n");

	strncpy (dest + len, src, l2);
	dest[len + l2] = '\0';

	return dest;
}
