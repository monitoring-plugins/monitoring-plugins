/*****************************************************************************

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
*****************************************************************************/

/* progname "check_tcp" changes depending on symlink called */
char *progname;
const char *revision = "$Revision$";
const char *copyright = "1999-2004";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

#ifdef HAVE_GNUTLS_OPENSSL_H
#  include <gnutls/openssl.h>
#else
#  ifdef HAVE_SSL_H
#    include <rsa.h>
#    include <crypto.h>
#    include <x509.h>
#    include <pem.h>
#    include <ssl.h>
#    include <err.h>
#  else
#    ifdef HAVE_OPENSSL_SSL_H
#      include <openssl/rsa.h>
#      include <openssl/crypto.h>
#      include <openssl/x509.h>
#      include <openssl/pem.h>
#      include <openssl/ssl.h>
#      include <openssl/err.h>
#    endif
#  endif
#endif

#ifdef HAVE_SSL
static int check_cert = FALSE;
static int days_till_exp;
static char *randbuff = "";
static SSL_CTX *ctx;
static SSL *ssl;
static X509 *server_cert;
static int connect_SSL (void);
# ifdef USE_OPENSSL
static int check_certificate (X509 **);
# endif /* USE_OPENSSL */
# define my_recv(buf, len) ((flags & FLAG_SSL) ? SSL_read(ssl, buf, len) : read(sd, buf, len))
#else
# define my_recv(buf, len) read(sd, buf, len)
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

static char timestamp[17] = "";
static int server_port = 0;
static char *server_address = NULL;
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

#define FLAG_SSL 0x01
#define FLAG_VERBOSE 0x02
#define FLAG_EXACT_MATCH 0x04
#define FLAG_TIME_WARN 0x08
#define FLAG_TIME_CRIT 0x10
#define FLAG_HIDE_OUTPUT 0x20
static size_t flags = FLAG_EXACT_MATCH;

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	int i;
	char *status = NULL;
	struct timeval tv;
	size_t len, match = -1;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* determine program- and service-name quickly */
	progname = strrchr(argv[0], '/');
	if(progname != NULL) progname++;
	else progname = argv[0];

	len = strlen(progname);
	if(len > 6 && !memcmp(progname, "check_", 6)) {
		SERVICE = progname + 6;
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
		EXPECT = "<?xml version=\'1.0\'?><stream:stream xmlns:stream=\'http://etherx.jabber.org/streams\'";
		QUIT = "</stream:stream>\n";
		flags |= FLAG_SSL | FLAG_HIDE_OUTPUT;
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
	/* fallthrough check, so it's supposed to use reverse matching */
	else if (strcmp (SERVICE, "TCP"))
		usage (_("CRITICAL - Generic check_tcp called with unknown service\n"));

	server_address = "127.0.0.1";
	server_port = PORT;
	server_send = SEND;
	server_quit = QUIT;
	status = NULL;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	if(flags & FLAG_VERBOSE) {
		printf("Using service %s\n", SERVICE);
		printf("Port: %d\n", PORT);
		printf("flags: 0x%x\n", flags);
	}

	if(EXPECT && !server_expect_count)
		server_expect_count++;

	/* set up the timer */
	signal (SIGALRM, socket_timeout_alarm_handler);
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	gettimeofday (&tv, NULL);
#ifdef HAVE_SSL
	if (flags & FLAG_SSL && check_cert == TRUE) {
		if (connect_SSL () != OK)
			die (STATE_CRITICAL,_("CRITICAL - Could not make SSL connection\n"));
#  ifdef USE_OPENSSL /* XXX gnutls does cert checking differently */
		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			result = check_certificate (&server_cert);
			X509_free(server_cert);
		}
		else {
			printf(_("CRITICAL - Cannot retrieve server certificate.\n"));
			result = STATE_CRITICAL;
		}
#  endif /* USE_OPENSSL */

		SSL_shutdown (ssl);
		SSL_free (ssl);
		SSL_CTX_free (ctx);
		close (sd);
		return result;
	}
	else if (flags & FLAG_SSL)
		result = connect_SSL ();
	else
#endif
		result = np_net_connect (server_address, server_port, &sd, PROTOCOL);

	if (result == STATE_CRITICAL)
		return STATE_CRITICAL;

	if (server_send != NULL) {		/* Something to send? */
#ifdef HAVE_SSL
		if (flags & FLAG_SSL)
			SSL_write(ssl, server_send, (int)strlen(server_send));
		else
#endif
			send (sd, server_send, strlen(server_send), 0);
	}

	if (delay > 0) {
		tv.tv_sec += delay;
		sleep (delay);
	}

	if(flags & FLAG_VERBOSE) {
		printf("server_expect_count: %d\n", server_expect_count);
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

			/* stop reading if user-forced or data-starved */
			if(i < sizeof(buffer) || (maxbytes && len >= maxbytes))
				break;

			if (maxbytes && len >= maxbytes)
				break;
		}

		/* no data when expected, so return critical */
		if (len == 0)
			die (STATE_CRITICAL, _("No data received from host\n"));

		/* force null-termination and strip whitespace from end of output */
		status[len--] = '\0';
		/* print raw output if we're debugging */
		if(flags & FLAG_VERBOSE)
			printf("received %d bytes from host\n#-raw-recv-------#\n%s\n#-raw-recv-------#\n",
			       len + 1, status);
		while(isspace(status[len])) status[len--] = '\0';

		for (i = 0; i < server_expect_count; i++) {
			match = -2;		/* tag it so we know if we tried and failed */
			if (flags & FLAG_VERBOSE)
				printf ("looking for [%s] %s [%s]\n", server_expect[i],
				        (flags & FLAG_EXACT_MATCH) ? "in beginning of" : "anywhere in",
				        status);

			/* match it. math first in short-circuit */
			if ((flags & FLAG_EXACT_MATCH && !strncmp(status, server_expect[i], strlen(server_expect[i]))) ||
			    (!(flags & FLAG_EXACT_MATCH) && strstr(status, server_expect[i])))
			{
				if(flags & FLAG_VERBOSE) puts("found it");
				match = i;
				break;
			}
		}
	}

	if (server_quit != NULL) {
#ifdef HAVE_SSL
		if (flags & FLAG_SSL) {
			SSL_write (ssl, server_quit, (int)strlen(server_quit));
			SSL_shutdown (ssl);
 			SSL_free (ssl);
 			SSL_CTX_free (ctx);
		}
		else
#endif
			send (sd, server_quit, strlen (server_quit), 0);
	}

	/* close the connection */
	if (sd)
		close (sd);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (flags & FLAG_TIME_CRIT && elapsed_time > critical_time)
		result = STATE_CRITICAL;
	else if (flags & FLAG_TIME_WARN && elapsed_time > warning_time)
		result = STATE_WARNING;

	/* did we get the response we hoped? */
	if(match == -2 && result != STATE_CRITICAL)
		result = STATE_WARNING;

	/* reset the alarm */
	alarm (0);

	/* this is a bit stupid, because we don't want to print the
	 * response time (which can look ok to the user) if we didn't get
	 * the response we were looking for. if-else */
	printf(_("%s %s - "), SERVICE, state_text(result));

	if(match == -2 && len && !(flags & FLAG_HIDE_OUTPUT))
		printf("Unexpected response from host: %s", status);
	else
		printf("%.3f second response time on port %d",
		       elapsed_time, server_port);

	if (match != -2 && !(flags & FLAG_HIDE_OUTPUT) && len)
		printf (" [%s]", status);

	/* perf-data doesn't apply when server doesn't talk properly,
	 * so print all zeroes on warn and crit */
	if(match == -2)
		printf ("|time=%fs;0.0;0.0;0.0;0.0", elapsed_time);
	else
		printf("|%s",
				fperfdata ("time", elapsed_time, "s",
		                   TRUE, warning_time,
		                   TRUE, critical_time,
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

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"critical-time", required_argument, 0, 'c'},
		{"warning-time", required_argument, 0, 'w'},
		{"critical-codes", required_argument, 0, 'C'},
		{"warning-codes", required_argument, 0, 'W'},
		{"timeout", required_argument, 0, 't'},
		{"protocol", required_argument, 0, 'P'},
		{"port", required_argument, 0, 'p'},
		{"send", required_argument, 0, 's'},
		{"expect", required_argument, 0, 'e'},
		{"maxbytes", required_argument, 0, 'm'},
		{"quit", required_argument, 0, 'q'},
		{"jail", required_argument, 0, 'j'},
		{"delay", required_argument, 0, 'd'},
		{"refuse", required_argument, 0, 'r'},
		{"mismatch", required_argument, 0, 'M'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
#ifdef HAVE_SSL
		{"ssl", no_argument, 0, 'S'},
		{"certificate", required_argument, 0, 'D'},
#endif
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
		c = getopt_long (argc, argv, "+hVv46H:s:e:q:m:c:w:t:p:C:W:d:Sr:jD:M:",
		                 longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':                 /* print short usage statement if args not parsable */
			usage2 (_("Unknown argument"), optarg);
		case 'h':                 /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':                 /* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'v':                 /* verbose mode */
			flags |= FLAG_VERBOSE;
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
			if (is_host (optarg) == FALSE)
				usage2 (_("Invalid hostname/address"), optarg);
			server_address = optarg;
			break;
		case 'c':                 /* critical */
			if (!is_intnonneg (optarg))
				usage4 (_("Critical threshold must be a positive integer"));
			else
				critical_time = strtod (optarg, NULL);
			flags |= FLAG_TIME_CRIT;
			break;
		case 'j':		  /* hide output */
			flags |= FLAG_HIDE_OUTPUT;
			break;
		case 'w':                 /* warning */
			if (!is_intnonneg (optarg))
				usage4 (_("Warning threshold must be a positive integer"));
			else
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
		case 's':
			server_send = optarg;
			break;
		case 'e': /* expect string (may be repeated) */
			EXPECT = NULL;
			flags &= ~FLAG_EXACT_MATCH;
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
		case 'q':
			asprintf(&server_quit, "%s\r\n", optarg);
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
			if (!is_intnonneg (optarg))
				usage2 (_("Invalid certificate expiration period"), optarg);
			days_till_exp = atoi (optarg);
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
		}
	}

	if (server_address == NULL)
		usage4 (_("You must provide a server address"));

	return TRUE;
}


/* SSL-specific functions */
#ifdef HAVE_SSL
static int
connect_SSL (void)
{
  SSL_METHOD *meth;

  /* Initialize SSL context */
  SSLeay_add_ssl_algorithms ();
  meth = SSLv23_client_method ();
  SSL_load_error_strings ();
  OpenSSL_add_all_algorithms();
  if ((ctx = SSL_CTX_new (meth)) == NULL)
    {
      printf (_("CRITICAL - Cannot create SSL context.\n"));
      return STATE_CRITICAL;
    }

  /* Initialize alarm signal handling */
  signal (SIGALRM, socket_timeout_alarm_handler);

  /* Set socket timeout */
  alarm (socket_timeout);

  /* Save start time */
  time (&start_time);

  /* Make TCP connection */
  if (my_tcp_connect (server_address, server_port, &sd) == STATE_OK && was_refused == FALSE)
    {
    /* Do the SSL handshake */
      if ((ssl = SSL_new (ctx)) != NULL)
      {
        SSL_set_fd (ssl, sd);
        if (SSL_connect(ssl) == 1)
          return OK;
        /* ERR_print_errors_fp (stderr); */
	printf (_("CRITICAL - Cannot make  SSL connection "));
#ifdef USE_OPENSSL /* XXX */
        ERR_print_errors_fp (stdout);
#endif /* USE_OPENSSL */
	/* printf("\n"); */
      }
      else
      {
        printf (_("CRITICAL - Cannot initiate SSL handshake.\n"));
      }
      SSL_free (ssl);
    }

  SSL_CTX_free (ctx);
  close (sd);

  return STATE_CRITICAL;
}

#ifdef USE_OPENSSL /* XXX */
static int
check_certificate (X509 ** certificate)
{
  ASN1_STRING *tm;
  int offset;
  struct tm stamp;
  int days_left;


  /* Retrieve timestamp of certificate */
  tm = X509_get_notAfter (*certificate);

  /* Generate tm structure to process timestamp */
  if (tm->type == V_ASN1_UTCTIME) {
    if (tm->length < 10) {
      printf (_("CRITICAL - Wrong time format in certificate.\n"));
      return STATE_CRITICAL;
    }
    else {
      stamp.tm_year = (tm->data[0] - '0') * 10 + (tm->data[1] - '0');
      if (stamp.tm_year < 50)
	stamp.tm_year += 100;
      offset = 0;
    }
  }
  else {
    if (tm->length < 12) {
      printf (_("CRITICAL - Wrong time format in certificate.\n"));
      return STATE_CRITICAL;
    }
    else {
                        stamp.tm_year =
			  (tm->data[0] - '0') * 1000 + (tm->data[1] - '0') * 100 +
			  (tm->data[2] - '0') * 10 + (tm->data[3] - '0');
                        stamp.tm_year -= 1900;
                        offset = 2;
    }
  }
        stamp.tm_mon =
	  (tm->data[2 + offset] - '0') * 10 + (tm->data[3 + offset] - '0') - 1;
        stamp.tm_mday =
	  (tm->data[4 + offset] - '0') * 10 + (tm->data[5 + offset] - '0');
        stamp.tm_hour =
	  (tm->data[6 + offset] - '0') * 10 + (tm->data[7 + offset] - '0');
        stamp.tm_min =
	  (tm->data[8 + offset] - '0') * 10 + (tm->data[9 + offset] - '0');
        stamp.tm_sec = 0;
        stamp.tm_isdst = -1;

        days_left = (mktime (&stamp) - time (NULL)) / 86400;
        snprintf
	  (timestamp, 16, "%02d/%02d/%04d %02d:%02d",
	   stamp.tm_mon + 1,
	   stamp.tm_mday, stamp.tm_year + 1900, stamp.tm_hour, stamp.tm_min);

        if (days_left > 0 && days_left <= days_till_exp) {
	  printf (_("Certificate expires in %d day(s) (%s).\n"), days_left, timestamp);
	  return STATE_WARNING;
        }
        if (days_left < 0) {
	  printf (_("Certificate expired on %s.\n"), timestamp);
	  return STATE_CRITICAL;
        }

        if (days_left == 0) {
	  printf (_("Certificate expires today (%s).\n"), timestamp);
	  return STATE_WARNING;
        }

        printf (_("Certificate will expire on %s.\n"), timestamp);

        return STATE_OK;
}
#  endif /* USE_OPENSSL */
#endif /* HAVE_SSL */


void
print_help (void)
{
	print_revision (progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf (_("This plugin tests %s connections with the specified host.\n\n"),
	        SERVICE);

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', "none");

	printf (_(UT_IPv46));

	printf (_("\
 -s, --send=STRING\n\
    String to send to the server\n\
 -e, --expect=STRING\n\
    String to expect in server response\n\
 -q, --quit=STRING\n\
    String to send server to initiate a clean close of the connection\n"));

	printf (_("\
 -r, --refuse=ok|warn|crit\n\
    Accept tcp refusals with states ok, warn, crit (default: crit)\n\
 -M, --mismatch=ok|warn|crit\n\
    Accept expected string mismatches with states ok, warn, crit (default: warn)\n\
 -j, --jail\n\
    Hide output from TCP socket\n\
 -m, --maxbytes=INTEGER\n\
    Close connection once more than this number of bytes are received\n\
 -d, --delay=INTEGER\n\
    Seconds to wait between sending string and polling for response\n"));

#ifdef HAVE_SSL
	printf (_("\
 -D, --certificate=INTEGER\n\
    Minimum number of days a certificate has to be valid.\n\
 -S, --ssl\n\
    Use SSL for the connection.\n"));
#endif

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf (_(UT_SUPPORT));
}


void
print_usage (void)
{
	printf ("\
Usage: %s -H host -p port [-w <warning time>] [-c <critical time>]\n\
                  [-s <send string>] [-e <expect string>] [-q <quit string>]\n\
                  [-m <maximum bytes>] [-d <delay>] [-t <timeout seconds>]\n\
                  [-r <refuse state>] [-M <mismatch state>] [-v] [-4|-6] [-j]\n\
                  [-D <days to cert expiry>] [-S <use SSL>]\n", progname);
}

