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
#include "version.h"
#include <stdarg.h>
#include <limits.h>

extern int timeout_interval;

char *my_basename (char *);
void support (void);
char *clean_revstring (const char *);
void print_revision (char *, const char *);
void terminate (int, const char *fmt, ...);
RETSIGTYPE timeout_alarm_handler (int);

int is_host (char *);
int is_dotted_quad (char *);
int is_hostname (char *);

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

void strip (char *);
char *strscpy (char *dest, const char *src);
char *strscat (char *dest, const char *src);
char *strnl (char *str);
char *ssprintf (char *str, const char *fmt, ...);
char *strpcpy (char *dest, const char *src, const char *str);
char *strpcat (char *dest, const char *src, const char *str);

#define LABELLEN 63
#define STRLEN 64
#define TXTBLK 128

#define max(a,b) ((a)>(b))?(a):(b)

/* **************************************************************************
 * max_state(result, STATE_x)
 * compares STATE_x to result and returns result if STATE_x is less than a based on the following
 * STATE_UNKNOWN < STATE_OK < STATE_WARNING < STATE_CRITICAL
 *
 * Note that numerically the above does not hold
 ****************************************************************************/

int
max_state(int a, int b)
{
	if(a == STATE_CRITICAL){
		return a;
	}
	else if (a == STATE_WARNING) {

		if (b == STATE_CRITICAL){
			return b;
		}else {
			return a;
		}
	} 
	else if (a == STATE_OK) {
		
		if ( b== STATE_CRITICAL || b == STATE_WARNING) {
			return b;
		}else{
			return a;
		}
	}
	else {
		/* a == UNKNOWN */
		return b;
	}
		

}

char *
my_basename (char *path)
{
	if (!strstr (path, "/"))
		return path;
	else
		return 1 + strrchr (path, '/');
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
print_revision (char *command_name, const char *revision_string)
{
	char plugin_revision[STRLEN];

	if (sscanf (revision_string, "$Revision: %[0-9.]", plugin_revision) != 1)
		strncpy (plugin_revision, "N/A", STRLEN);
	printf ("%s (nagios-plugins %s) %s\n",
					my_basename (command_name), VERSION, plugin_revision);
	printf
		("The nagios plugins come with ABSOLUTELY NO WARRANTY. You may redistribute\n"
		 "copies of the plugins under the terms of the GNU General Public License.\n"
		 "For more information about these matters, see the file named COPYING.\n");

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
is_host (char *address)
{
	if (is_dotted_quad (address) || is_hostname (address))
		return (TRUE);
	return (FALSE);
}

int
is_dotted_quad (char *address)
{
	int o1, o2, o3, o4;
	char c[1];

	if (sscanf (address, "%d.%d.%d.%d%c", &o1, &o2, &o3, &o4, c) != 4)
		return FALSE;
	else if (o1 > 255 || o2 > 255 || o3 > 255 || o4 > 255)
		return FALSE;
	else if (o1 < 0 || o2 < 0 || o3 < 0 || o4 < 0)
		return FALSE;
	else
		return TRUE;
}

/* from RFC-1035
 * 
 * The labels must follow the rules for ARPANET host names.  They must
 * start with a letter, end with a letter or digit, and have as interior
 * characters only letters, digits, and hyphen.  There are also some
 * restrictions on the length.  Labels must be 63 characters or less. */

int
is_hostname (char *s1)
{
	if (strlen (s1) > 63)
		return FALSE;
	if (strcspn
			(s1,
			 "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUWVXYZ0123456789-.") !=
			0) return FALSE;
	if (strspn (s1, "0123456789-.") == 1)
		return FALSE;
	while ((s1 = index (s1, '.'))) {
		s1++;
		if (strspn (s1, "0123456789-.") == 1) {
			printf ("%s\n", s1);
			return FALSE;
		}
	}
	return TRUE;
}

int
is_numeric (char *number)
{
	char tmp[1];
	float x;
	if (sscanf (number, "%f%c", &x, tmp) == 1)
		return (TRUE);
	return (FALSE);
}

int
is_positive (char *number)
{
	if (is_numeric (number) && atof (number) > 0.0)
		return (TRUE);
	return (FALSE);
}

int
is_negative (char *number)
{
	if (is_numeric (number) && atof (number) < 0.0)
		return (TRUE);
	return (FALSE);
}

int
is_nonnegative (char *number)
{
	if (is_numeric (number) && atof (number) >= 0.0)
		return (TRUE);
	return (FALSE);
}

int
is_percentage (char *number)
{
	int x;
	if (is_numeric (number) && (x = atof (number)) >= 0 && x <= 100)
		return (TRUE);
	return (FALSE);
}

int
is_integer (char *number)
{
	long int n;

	if (strspn (number, "-0123456789 ") != strlen (number))
		return (FALSE);

	n = strtol (number, NULL, 10);
	if (errno != ERANGE && n >= INT_MIN && n <= INT_MAX)
		return (TRUE);
	return (FALSE);
}

int
is_intpos (char *number)
{
	if (is_integer (number) && atoi (number) > 0)
		return (TRUE);
	return (FALSE);
}

int
is_intneg (char *number)
{
	if (is_integer (number) && atoi (number) < 0)
		return (TRUE);
	return (FALSE);
}

int
is_intnonneg (char *number)
{
	if (is_integer (number) && atoi (number) >= 0)
		return (TRUE);
	return (FALSE);
}

int
is_intpercent (char *number)
{
	int i;
	if (is_integer (number) && (i = atoi (number)) >= 0 && i <= 100)
		return (TRUE);
	return (FALSE);
}

int
is_option (char *str)
{
	if (strspn (str, "-") == 1 || strspn (str, "-") == 2)
		return TRUE;
	return FALSE;
}




double
delta_time (struct timeval tv)
{
	struct timeval now;
	struct timezone tz;
	double et;

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
	size_t len;

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
strscat (char *dest, const char *src)
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
 * Does a formatted print to a string variable
 *
 * Given a pointer destination string, which may or may not already
 * hold some text, and a source string with additional text (possibly
 * NULL or empty), returns a pointer to a string that cntains the
 * results of the specified formatted print
 *
 * Example:
 *
 * char *str=NULL;
 * str = ssprintf(str,"%d %s",1,"string");
 *
 *****************************************************************************/

char *
ssprintf (char *ptr, const char *fmt, ...)
{
	va_list ap;
	int nchars;
	size_t size;
	char *str = NULL;

	if (str == NULL) {
		str = malloc (TXTBLK);
		if (str == NULL)
			terminate (STATE_UNKNOWN, "malloc failed in ssprintf");
		size = TXTBLK;
	}
	else
		size = max (strlen (str), TXTBLK);

	va_start (ap, fmt);

	while (1) {

		nchars = vsnprintf (str, size, fmt, ap);

		if (nchars > -1)
			if (nchars < (int) size) {
				va_end (ap);
				str[nchars] = '\0';
				if (ptr)
					free (ptr);
				return str;
			}
			else {
				size = (size_t) (nchars + 1);
			}

		else
			size *= 2;

		str = realloc (str, size);

		if (str == NULL)
			terminate (STATE_UNKNOWN, "realloc failed in ssprintf");
	}

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
