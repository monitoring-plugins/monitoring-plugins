/* header file for nagios plugins utils.c */

/* this file should be included in all plugins */

/* The purpose of this package is to provide safer alternantives to C
functions that might otherwise be vulnerable to hacking. This
currently includes a standard suite of validation routines to be sure
that an string argument acually converts to its intended type and a
suite of string handling routine that do their own memory management
in order to resist overflow attacks. In addition, a few functions are
provided to standardize version and error reporting accross the entire
suite of plugins. */

/* Standardize version information, termination */

void support (void);
char *clean_revstring (const char *revstring);
void print_revision (const char *, const char *);
void die (int result, const char *fmt, ...);
void terminate (int result, const char *msg, ...);

/* Handle timeouts */

#ifdef LOCAL_TIMEOUT_ALARM_HANDLER
extern int timeout_interval;
RETSIGTYPE timeout_alarm_handler (int);
#else
int timeout_interval = DEFAULT_SOCKET_TIMEOUT;
extern RETSIGTYPE timeout_alarm_handler (int);
#endif

time_t start_time, end_time;

/* Test input types */

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

int is_option (char *);

/* generalized timer that will do milliseconds if available */
#ifndef HAVE_STRUCT_TIMEVAL
struct timeval {
	long tv_sec;        /* seconds */
	long tv_usec;  /* microseconds */
};
#endif

#ifndef HAVE_GETTIMEOFDAY
int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif

double delta_time (struct timeval tv);

/* Handle strings safely */

void strip (char *buffer);
char *strscpy (char *dest, const char *src);
char *strscat (char *dest, const char *src);
char *strnl (char *str);
char *ssprintf (char *str, const char *fmt, ...); /* deprecate for asprintf */
char *strpcpy (char *dest, const char *src, const char *str);
char *strpcat (char *dest, const char *src, const char *str);

int max_state (int a, int b);

void usage (char *msg);
void usage2(char *msg, char *arg);
void usage3(char *msg, char arg);

char *state_text (int result);

#define max(a,b) (((a)>(b))?(a):(b))

/* The idea here is that, although not every plugin will use all of these, 
   most will or should.  Therefore, for consistency, these very common 
   options should have only these meanings throughout the overall suite */

#define STD_LONG_OPTS \
{"version",no_argument,0,'V'},\
{"verbose",no_argument,0,'v'},\
{"help",no_argument,0,'h'},\
{"timeout",required_argument,0,'t'},\
{"critical",required_argument,0,'c'},\
{"warning",required_argument,0,'w'},\
{"hostname",required_argument,0,'H'}

#define COPYRIGHT "Copyright (c) %s Nagios Plugin Development Team\n\
\t<%s>\n\n"

#define UT_HELP_VRSN "\
\nOptions:\n\
 -h, --help\n\
    Print detailed help screen\n\
 -V, --version\n\
    Print version information\n"

#define UT_HOST_PORT "\
 -H, --hostname=ADDRESS\n\
    Host name or IP Address\n\
 -%c, --port=INTEGER\n\
    Port number (default: %s)\n"

#define UT_IPv46 "\
 -4, --use-ipv4\n\
    Use IPv4 connection\n\
 -6, --use-ipv6\n\
    Use IPv6 connection\n"

#define UT_VERBOSE "\
 -v, --verbose\n\
    Show details for command-line debugging (Nagios may truncate output)\n"

#define UT_WARN_CRIT "\
 -w, --warning=DOUBLE\n\
    Response time to result in warning status (seconds)\n\
 -c, --critical=DOUBLE\n\
    Response time to result in critical status (seconds)\n"

#define UT_TIMEOUT "\
 -t, --timeout=INTEGER\n\
    Seconds before connection times out (default: %d)\n"

#define UT_SUPPORT "\n\
Send email to nagios-users@lists.sourceforge.net if you have questions\n\
regarding use of this software. To submit patches or suggest improvements,\n\
send email to nagiosplug-devel@lists.sourceforge.net\n"

#define UT_NOWARRANTY "\
The nagios plugins come with ABSOLUTELY NO WARRANTY. You may redistribute\n\
copies of the plugins under the terms of the GNU General Public License.\n\
For more information about these matters, see the file named COPYING.\n"
