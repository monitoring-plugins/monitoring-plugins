/*****************************************************************************
*
* Nagios check_tcp plugin
*
* License: GPL
* Copyright (c) 1999-2013 Nagios Plugins Development Team
*
* Description:
*
* This file contains the check_tcp plugin
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
* $Id$
*
*****************************************************************************/

/* progname "check_tcp" changes depending on symlink called */
char *progname;
const char *copyright = "1999-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include <ctype.h>

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "utils_tcp.h"

#include <sys/select.h>

#ifdef HAVE_SSL
static int check_cert = FALSE;
static int days_till_exp_warn, days_till_exp_crit;
# define my_recv(buf, len) ((flags & FLAG_SSL) ? np_net_ssl_read(buf, len) : read(sd, buf, len))
# define my_send(buf, len) ((flags & FLAG_SSL) ? np_net_ssl_write(buf, len) : send(sd, buf, len, 0))
#else
# define my_recv(buf, len) read(sd, buf, len)
# define my_send(buf, len) send(sd, buf, len, 0)
#endif

/* int my_recv(char *, size_t); */
static int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

#define EXPECT server_expect[0]
static char *SERVICE = "TCP";
static char *SEND = NULL;
static char *QUIT = NULL;
static int PROTOCOL = IPPROTO_TCP; /* most common is default */
static int PORT = 0;
static int READ_TIMEOUT = 2;

static int server_port = 0;
static char *server_address = NULL;
static int host_specified = FALSE;
static char *server_send = NULL;
static char *server_quit = NULL;
static char **server_expect;
static size_t server_expect_count = 0;
static size_t maxbytes = 0;
static char **warn_codes = NULL;
static size_t warn_codes_count = 0;
static char **crit_codes = NULL;
static size_t crit_codes_count = 0;
static unsigned int delay = 0;
static double warning_time = 0;
static double critical_time = 0;
static double elapsed_time = 0;
static long microsec;
static int sd = 0;
#define MAXBUF 1024
static char buffer[MAXBUF];
static int expect_mismatch_state = STATE_WARNING;
static int match_flags = NP_MATCH_EXACT;

#define FLAG_SSL 0x01
#define FLAG_VERBOSE 0x02
#define FLAG_TIME_WARN 0x04
#define FLAG_TIME_CRIT 0x08
#define FLAG_HIDE_OUTPUT 0x10
static size_t flags;

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	int i;
	char *status = NULL;
	struct timeval tv;
	struct timeval timeout;
	size_t len;
	int match = -1;
	fd_set rfds;

	FD_ZERO(&rfds);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* determine program- and service-name quickly */
	progname = strrchr(argv[0], '/');
	if(progname != NULL) progname++;
	else progname = argv[0];

	len = strlen(progname);
	if(len > 6 && !memcmp(progname, "check_", 6)) {
		SERVICE = strdup(progname + 6);
		for(i = 0; i < len - 6; i++)
			SERVICE[i] = toupper(SERVICE[i]);
	}

	/* set up a resonable buffer at first (will be realloc()'ed if
	 * user specifies other options) */
	server_expect = calloc(sizeof(char *), 2);

	/* determine defaults for this service's protocol */
	if (!strncmp(SERVICE, "UDP", 3)) {
		PROTOCOL = IPPROTO_UDP;
	}
	else if (!strncmp(SERVICE, "FTP", 3)) {
		EXPECT = "220";
		QUIT = "QUIT\r\n";
		PORT = 21;
	}
	else if (!strncmp(SERVICE, "POP", 3) || !strncmp(SERVICE, "POP3", 4)) {
		EXPECT = "+OK";
		QUIT = "QUIT\r\n";
		PORT = 110;
	}
	else if (!strncmp(SERVICE, "SMTP", 4)) {
		EXPECT = "220";
		QUIT = "QUIT\r\n";
		PORT = 25;
	}
	else if (!strncmp(SERVICE, "IMAP", 4)) {
		EXPECT = "* OK";
		QUIT = "a1 LOGOUT\r\n";
		PORT = 143;
	}
#ifdef HAVE_SSL
	else if (!strncmp(SERVICE, "SIMAP", 5)) {
		EXPECT = "* OK";
		QUIT = "a1 LOGOUT\r\n";
		flags |= FLAG_SSL;
		PORT = 993;
	}
	else if (!strncmp(SERVICE, "SPOP", 4)) {
		EXPECT = "+OK";
		QUIT = "QUIT\r\n";
		flags |= FLAG_SSL;
		PORT = 995;
	}
	else if (!strncmp(SERVICE, "SSMTP", 5)) {
		EXPECT = "220";
		QUIT = "QUIT\r\n";
		flags |= FLAG_SSL;
		PORT = 465;
	}
	else if (!strncmp(SERVICE, "JABBER", 6)) {
		SEND = "<stream:stream to=\'host\' xmlns=\'jabber:client\' xmlns:stream=\'http://etherx.jabber.org/streams\'>\n";
		EXPECT = "<?xml version=\'1.0\'?><stream:stream xmlns=\'jabber:client\' xmlns:stream=\'http://etherx.jabber.org/streams\'";
		QUIT = "</stream:stream>\n";
		flags |= FLAG_HIDE_OUTPUT;
		PORT = 5222;
	}
	else if (!strncmp (SERVICE, "NNTPS", 5)) {
		server_expect_count = 2;
		server_expect[0] = "200";
		server_expect[1] = "201";
		QUIT = "QUIT\r\n";
		flags |= FLAG_SSL;
		PORT = 563;
	}
#endif
	else if (!strncmp (SERVICE, "NNTP", 4)) {
		server_expect_count = 2;
		server_expect = malloc(sizeof(char *) * server_expect_count);
		server_expect[0] = strdup("200");
		server_expect[1] = strdup("201");
		QUIT = "QUIT\r\n";
		PORT = 119;
	}
	else if (!strncmp(SERVICE, "CLAMD", 5)) {
		SEND = "PING";
		EXPECT = "PONG";
		QUIT = NULL;
		PORT = 3310;
	}
	/* fallthrough check, so it's supposed to use reverse matching */
	else if (strcmp (SERVICE, "TCP"))
		usage (_("CRITICAL - Generic check_tcp called with unknown service\n"));

	server_address = "127.0.0.1";
	server_port = PORT;
	server_send = SEND;
	server_quit = QUIT;
	status = NULL;

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	if(flags & FLAG_VERBOSE) {
		printf("Using service %s\n", SERVICE);
		printf("Port: %d\n", server_port);
		printf("flags: 0x%x\n", (int)flags);
	}

	if(EXPECT && !server_expect_count)
		server_expect_count++;

	if(PROTOCOL==IPPROTO_UDP && !(server_expect_count && server_send)){
		usage(_("With UDP checks, a send/expect string must be specified."));
	}

	/* set up the timer */
	signal (SIGALRM, socket_timeout_alarm_handler);
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	gettimeofday (&tv, NULL);

	result = np_net_connect (server_address, server_port, &sd, PROTOCOL);
	if (result == STATE_CRITICAL) return STATE_CRITICAL;

#ifdef HAVE_SSL
	if (flags & FLAG_SSL){
		result = np_net_ssl_init(sd);
		if (result == STATE_OK && check_cert == TRUE) {
			result = np_net_ssl_check_cert(days_till_exp_warn, days_till_exp_crit);
		}
	}
	if(result != STATE_OK){
		np_net_ssl_cleanup();
		if(sd) close(sd);
		return result;
	}
#endif /* HAVE_SSL */

	if (server_send != NULL) {		/* Something to send? */
		my_send(server_send, strlen(server_send));
	}

	if (delay > 0) {
		tv.tv_sec += delay;
		sleep (delay);
	}

	if(flags & FLAG_VERBOSE) {
		if (server_send) {
			printf("Send string: %s\n", server_send);
		}
		if (server_quit) {
			printf("Quit string: %s\n", server_quit);
		}
		printf("server_expect_count: %d\n", (int)server_expect_count);
		for(i = 0; i < server_expect_count; i++)
			printf("\t%d: %s\n", i, server_expect[i]);
	}

	/* if(len) later on, we know we have a non-NULL response */
	len = 0;
	if (server_expect_count) {

		/* watch for the expect string */
		while ((i = my_recv(buffer, sizeof(buffer))) > 0) {
			status = realloc(status, len + i + 1);
			memcpy(&status[len], buffer, i);
			len += i;
			status[len] = '\0';

			/* stop reading if user-forced */
			if (maxbytes && len >= maxbytes)
				break;

			if ((match = np_expect_match(status,
			    server_expect,
			    server_expect_count,
			    match_flags)) != NP_MATCH_RETRY)
				break;

			/* some protocols wait for further input, so make sure we don't wait forever */
			FD_SET(sd, &rfds);
			timeout.tv_sec  = READ_TIMEOUT;
			timeout.tv_usec = 0;
			if(select(sd + 1, &rfds, NULL, NULL, &timeout) <= 0)
				break;
		}
		if (match == NP_MATCH_RETRY)
			match = NP_MATCH_FAILURE;

		/* no data when expected, so return critical */
		if (len == 0)
			die (STATE_CRITICAL, _("No data received from host\n"));

		/* print raw output if we're debugging */
		if(flags & FLAG_VERBOSE)
			printf("received %d bytes from host\n#-raw-recv-------#\n%s\n#-raw-recv-------#\n",
			       (int)len + 1, status);
		/* strip whitespace from end of output */
		while(--len > 0 && isspace(status[len]))
			status[len] = '\0';
	}

	if (server_quit != NULL) {
		my_send(server_quit, strlen(server_quit));
	}
#ifdef HAVE_SSL
	np_net_ssl_cleanup();
#endif
	if (sd) close (sd);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (flags & FLAG_TIME_CRIT && elapsed_time > critical_time)
		result = STATE_CRITICAL;
	else if (flags & FLAG_TIME_WARN && elapsed_time > warning_time)
		result = STATE_WARNING;

	/* did we get the response we hoped? */
	if(match == NP_MATCH_FAILURE && result != STATE_CRITICAL)
		result = expect_mismatch_state;

	/* reset the alarm */
	alarm (0);

	/* this is a bit stupid, because we don't want to print the
	 * response time (which can look ok to the user) if we didn't get
	 * the response we were looking for. if-else */
	printf("%s %s - ", SERVICE, state_text(result));

	if(match == NP_MATCH_FAILURE && len && !(flags & FLAG_HIDE_OUTPUT))
		printf("Unexpected response from host/socket: %s", status);
	else {
		if(match == NP_MATCH_FAILURE)
			printf("Unexpected response from host/socket on ");
		else
			printf("%.3f second response time on ", elapsed_time);
		if(server_address[0] != '/')
			printf("port %d", server_port);
		else
			printf("socket %s", server_address);
	}

	if (match != NP_MATCH_FAILURE && !(flags & FLAG_HIDE_OUTPUT) && len)
		printf (" [%s]", status);

	/* perf-data doesn't apply when server doesn't talk properly,
	 * so print all zeroes on warn and crit. Use fperfdata since
	 * localisation settings can make different outputs */
	if(match == NP_MATCH_FAILURE)
		printf ("|%s",
				fperfdata ("time", elapsed_time, "s",
				(flags & FLAG_TIME_WARN ? TRUE : FALSE), 0,
				(flags & FLAG_TIME_CRIT ? TRUE : FALSE), 0,
				TRUE, 0,
				TRUE, socket_timeout)
			);
	else
		printf("|%s",
				fperfdata ("time", elapsed_time, "s",
				(flags & FLAG_TIME_WARN ? TRUE : FALSE), warning_time,
				(flags & FLAG_TIME_CRIT ? TRUE : FALSE), critical_time,
				TRUE, 0,
				TRUE, socket_timeout)
			);

	putchar('\n');
	return result;
}



/* process command-line arguments */
static int
process_arguments (int argc, char **argv)
{
	int c;
	int escape = 0;
	char *temp;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"critical-codes", required_argument, 0, 'C'},
		{"warning-codes", required_argument, 0, 'W'},
		{"timeout", required_argument, 0, 't'},
		{"protocol", required_argument, 0, 'P'}, /* FIXME: Unhandled */
		{"port", required_argument, 0, 'p'},
		{"escape", no_argument, 0, 'E'},
		{"all", no_argument, 0, 'A'},
		{"send", required_argument, 0, 's'},
		{"expect", required_argument, 0, 'e'},
		{"maxbytes", required_argument, 0, 'm'},
		{"quit", required_argument, 0, 'q'},
		{"jail", no_argument, 0, 'j'},
		{"delay", required_argument, 0, 'd'},
		{"refuse", required_argument, 0, 'r'},
		{"mismatch", required_argument, 0, 'M'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"ssl", no_argument, 0, 'S'},
		{"certificate", required_argument, 0, 'D'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		usage4 (_("No arguments found"));

	/* backwards compatibility */
	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	if (!is_option (argv[1])) {
		server_address = argv[1];
		argv[1] = argv[0];
		argv = &argv[1];
		argc--;
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVv46EAH:s:e:q:m:c:w:t:p:C:W:d:Sr:jD:M:",
		                 longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':                 /* print short usage statement if args not parsable */
			usage5 ();
		case 'h':                 /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':                 /* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'v':                 /* verbose mode */
			flags |= FLAG_VERBOSE;
			match_flags |= NP_MATCH_VERBOSE;
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage4 (_("IPv6 support not available"));
#endif
			break;
		case 'H':                 /* hostname */
			host_specified = TRUE;
			server_address = optarg;
			break;
		case 'c':                 /* critical */
			critical_time = strtod (optarg, NULL);
			flags |= FLAG_TIME_CRIT;
			break;
		case 'j':		  /* hide output */
			flags |= FLAG_HIDE_OUTPUT;
			break;
		case 'w':                 /* warning */
			warning_time = strtod (optarg, NULL);
			flags |= FLAG_TIME_WARN;
			break;
		case 'C':
			crit_codes = realloc (crit_codes, ++crit_codes_count);
			crit_codes[crit_codes_count - 1] = optarg;
			break;
		case 'W':
			warn_codes = realloc (warn_codes, ++warn_codes_count);
			warn_codes[warn_codes_count - 1] = optarg;
			break;
		case 't':                 /* timeout */
			if (!is_intpos (optarg))
				usage4 (_("Timeout interval must be a positive integer"));
			else
				socket_timeout = atoi (optarg);
			break;
		case 'p':                 /* port */
			if (!is_intpos (optarg))
				usage4 (_("Port must be a positive integer"));
			else
				server_port = atoi (optarg);
			break;
		case 'E':
			escape = 1;
			break;
		case 's':
			if (escape)
				server_send = np_escaped_string(optarg);
			else
				xasprintf(&server_send, "%s", optarg);
			break;
		case 'e': /* expect string (may be repeated) */
			match_flags &= ~NP_MATCH_EXACT;
			if (server_expect_count == 0)
				server_expect = malloc (sizeof (char *) * (++server_expect_count));
			else
				server_expect = realloc (server_expect, sizeof (char *) * (++server_expect_count));
			server_expect[server_expect_count - 1] = optarg;
			break;
		case 'm':
			if (!is_intpos (optarg))
				usage4 (_("Maxbytes must be a positive integer"));
			else
				maxbytes = strtol (optarg, NULL, 0);
			break;
		case 'q':
			if (escape)
				server_quit = np_escaped_string(optarg);
			else
				xasprintf(&server_quit, "%s\r\n", optarg);
			break;
		case 'r':
			if (!strncmp(optarg,"ok",2))
				econn_refuse_state = STATE_OK;
			else if (!strncmp(optarg,"warn",4))
				econn_refuse_state = STATE_WARNING;
			else if (!strncmp(optarg,"crit",4))
				econn_refuse_state = STATE_CRITICAL;
			else
				usage4 (_("Refuse must be one of ok, warn, crit"));
			break;
		case 'M':
			if (!strncmp(optarg,"ok",2))
				expect_mismatch_state = STATE_OK;
			else if (!strncmp(optarg,"warn",4))
				expect_mismatch_state = STATE_WARNING;
			else if (!strncmp(optarg,"crit",4))
				expect_mismatch_state = STATE_CRITICAL;
			else
				usage4 (_("Mismatch must be one of ok, warn, crit"));
			break;
		case 'd':
			if (is_intpos (optarg))
				delay = atoi (optarg);
			else
				usage4 (_("Delay must be a positive integer"));
			break;
		case 'D': /* Check SSL cert validity - days 'til certificate expiration */
#ifdef HAVE_SSL
#  ifdef USE_OPENSSL /* XXX */
			if ((temp=strchr(optarg,','))!=NULL) {
			    *temp='\0';
			    if (!is_intnonneg (optarg))
                               usage2 (_("Invalid certificate expiration period"), optarg);				 days_till_exp_warn = atoi(optarg);
			    *temp=',';
			    temp++;
			    if (!is_intnonneg (temp))
				usage2 (_("Invalid certificate expiration period"), temp);
			    days_till_exp_crit = atoi (temp);
			}
			else {
			    days_till_exp_crit=0;
			    if (!is_intnonneg (optarg))
				usage2 (_("Invalid certificate expiration period"), optarg);
			    days_till_exp_warn = atoi (optarg);
			}
			check_cert = TRUE;
			flags |= FLAG_SSL;
			break;
#  endif /* USE_OPENSSL */
#endif
			/* fallthrough if we don't have ssl */
		case 'S':
#ifdef HAVE_SSL
			flags |= FLAG_SSL;
#else
			die (STATE_UNKNOWN, _("Invalid option - SSL is not available"));
#endif
			break;
		case 'A':
			match_flags |= NP_MATCH_ALL;
			break;
		}
	}

	c = optind;
	if(host_specified == FALSE && c < argc)
		server_address = strdup (argv[c++]);

	if (server_address == NULL)
		usage4 (_("You must provide a server address"));
	else if (server_address[0] != '/' && is_host (server_address) == FALSE)
		die (STATE_CRITICAL, "%s %s - %s: %s\n", SERVICE, state_text(STATE_CRITICAL), _("Invalid hostname, address or socket"), server_address);

	return TRUE;
}


void
print_help (void)
{
	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin tests %s connections with the specified host (or unix socket).\n\n"),
	        SERVICE);

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', "none");

	printf (UT_IPv46);

	printf (" %s\n", "-E, --escape");
  printf ("    %s\n", _("Can use \\n, \\r, \\t or \\ in send or quit string. Must come before send or quit option"));
  printf ("    %s\n", _("Default: nothing added to send, \\r\\n added to end of quit"));
  printf (" %s\n", "-s, --send=STRING");
  printf ("    %s\n", _("String to send to the server"));
  printf (" %s\n", "-e, --expect=STRING");
  printf ("    %s %s\n", _("String to expect in server response"), _("(may be repeated)"));
  printf (" %s\n", "-A, --all");
  printf ("    %s\n", _("All expect strings need to occur in server response. Default is any"));
  printf (" %s\n", "-q, --quit=STRING");
  printf ("    %s\n", _("String to send server to initiate a clean close of the connection"));
  printf (" %s\n", "-r, --refuse=ok|warn|crit");
  printf ("    %s\n", _("Accept TCP refusals with states ok, warn, crit (default: crit)"));
  printf (" %s\n", "-M, --mismatch=ok|warn|crit");
  printf ("    %s\n", _("Accept expected string mismatches with states ok, warn, crit (default: warn)"));
  printf (" %s\n", "-j, --jail");
  printf ("    %s\n", _("Hide output from TCP socket"));
  printf (" %s\n", "-m, --maxbytes=INTEGER");
  printf ("    %s\n", _("Close connection once more than this number of bytes are received"));
  printf (" %s\n", "-d, --delay=INTEGER");
  printf ("    %s\n", _("Seconds to wait between sending string and polling for response"));

#ifdef HAVE_SSL
	printf (" %s\n", "-D, --certificate=INTEGER[,INTEGER]");
  printf ("    %s\n", _("Minimum number of days a certificate has to be valid."));
  printf ("    %s\n", _("1st is #days for warning, 2nd is critical (if not specified - 0)."));
  printf (" %s\n", "-S, --ssl");
  printf ("    %s\n", _("Use SSL for the connection."));
#endif

	printf (UT_WARN_CRIT);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

	printf (UT_SUPPORT);
}


void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
  printf ("%s -H host -p port [-w <warning time>] [-c <critical time>] [-s <send string>]\n",progname);
  printf ("[-e <expect string>] [-q <quit string>][-m <maximum bytes>] [-d <delay>]\n");
  printf ("[-t <timeout seconds>] [-r <refuse state>] [-M <mismatch state>] [-v] [-4|-6] [-j]\n");
  printf ("[-D <warn days cert expire>[,<crit days cert expire>]] [-S <use SSL>] [-E]\n");
}
