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

#ifdef HAVE_SSL_H
#  include <rsa.h>
#  include <crypto.h>
#  include <x509.h>
#  include <pem.h>
#  include <ssl.h>
#  include <err.h>
#else
#  ifdef HAVE_OPENSSL_SSL_H
#    include <openssl/rsa.h>
#    include <openssl/crypto.h>
#    include <openssl/x509.h>
#    include <openssl/pem.h>
#    include <openssl/ssl.h>
#    include <openssl/err.h>
#  endif
#endif

#ifdef HAVE_SSL
int check_cert = FALSE;
int days_till_exp;
char *randbuff = "";
SSL_CTX *ctx;
SSL *ssl;
X509 *server_cert;
int connect_SSL (void);
int check_certificate (X509 **);
#endif

enum {
	TCP_PROTOCOL = 1,
	UDP_PROTOCOL = 2,
	MAXBUF = 1024
};

int process_arguments (int, char **);
int my_recv (void);
void print_help (void);
void print_usage (void);

char *SERVICE = NULL;
char *SEND = NULL;
char *EXPECT = NULL;
char *QUIT = NULL;
int PROTOCOL = 0;
int PORT = 0;

char timestamp[17] = "";
int server_port = 0;
char *server_address = NULL;
char *server_send = NULL;
char *server_quit = NULL;
char **server_expect = NULL;
size_t server_expect_count = 0;
int maxbytes = 0;
char **warn_codes = NULL;
size_t warn_codes_count = 0;
char **crit_codes = NULL;
size_t crit_codes_count = 0;
unsigned int delay = 0;
double warning_time = 0;
int check_warning_time = FALSE;
double critical_time = 0;
int check_critical_time = FALSE;
int hide_output = FALSE;
double elapsed_time = 0;
long microsec;
int verbose = FALSE;
int use_ssl = FALSE;
int sd = 0;
char *buffer;



int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;
	int i;
	char *status;
	struct timeval tv;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (strstr (argv[0], "check_udp")) {
		progname = strdup ("check_udp");
		SERVICE = strdup ("UDP");
		SEND = NULL;
		EXPECT = NULL;
		QUIT = NULL;
		PROTOCOL = UDP_PROTOCOL;
		PORT = 0;
	}
	else if (strstr (argv[0], "check_tcp")) {
		progname = strdup ("check_tcp");
		SERVICE = strdup ("TCP");
		SEND = NULL;
		EXPECT = NULL;
		QUIT = NULL;
		PROTOCOL = TCP_PROTOCOL;
		PORT = 0;
	}
	else if (strstr (argv[0], "check_ftp")) {
		progname = strdup ("check_ftp");
		SERVICE = strdup ("FTP");
		SEND = NULL;
		EXPECT = strdup ("220");
		QUIT = strdup ("QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 21;
	}
	else if (strstr (argv[0], "check_smtp")) {
		progname = strdup ("check_smtp");
		SERVICE = strdup ("SMTP");
		SEND = NULL;
		EXPECT = strdup ("220");
		QUIT = strdup ("QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 25;
	}
	else if (strstr (argv[0], "check_pop")) {
		progname = strdup ("check_pop");
		SERVICE = strdup ("POP");
		SEND = NULL;
		EXPECT = strdup ("+OK");
		QUIT = strdup ("QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 110;
	}
	else if (strstr (argv[0], "check_imap")) {
		progname = strdup ("check_imap");
		SERVICE = strdup ("IMAP");
		SEND = NULL;
		EXPECT = strdup ("* OK");
		QUIT = strdup ("a1 LOGOUT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 143;
	}
#ifdef HAVE_SSL
	else if (strstr(argv[0],"check_simap")) {
		progname = strdup ("check_simap");
		SERVICE = strdup ("SIMAP");
		SEND=NULL;
		EXPECT = strdup ("* OK");
		QUIT = strdup ("a1 LOGOUT\r\n");
		PROTOCOL=TCP_PROTOCOL;
		use_ssl=TRUE;
		PORT=993;
	}
	else if (strstr(argv[0],"check_spop")) {
		progname = strdup ("check_spop");
		SERVICE = strdup ("SPOP");
		SEND=NULL;
		EXPECT = strdup ("+OK");
		QUIT = strdup ("QUIT\r\n");
		PROTOCOL=TCP_PROTOCOL;
		use_ssl=TRUE;
		PORT=995;
	}
	else if (strstr(argv[0],"check_jabber")) {
		progname = strdup("check_jabber");
		SERVICE = strdup("JABBER");
		SEND = strdup("<stream:stream to=\'host\' xmlns=\'jabber:client\' xmlns:stream=\'http://etherx.jabber.org/streams\'>\n");
		EXPECT = strdup("<?xml version=\'1.0\'?><stream:stream xmlns:stream=\'http://etherx.jabber.org/streams\'");
		QUIT = strdup("</stream:stream>\n");
		PROTOCOL=TCP_PROTOCOL;
		use_ssl=TRUE;
		PORT = 5222;
	}
       else if (strstr (argv[0], "check_nntps")) {
		progname = strdup("check_nntps");
		SERVICE = strdup("NNTPS");
		SEND = NULL;
		EXPECT = NULL;
		server_expect = realloc (server_expect, ++server_expect_count);
		asprintf (&server_expect[server_expect_count - 1], "200");
		server_expect = realloc (server_expect, ++server_expect_count);
		asprintf (&server_expect[server_expect_count - 1], "201");
		QUIT = strdup("QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		use_ssl=TRUE;
		PORT = 563;
}

#endif
	else if (strstr (argv[0], "check_nntp")) {
		progname = strdup ("check_nntp");
		SERVICE = strdup ("NNTP");
		SEND = NULL;
		EXPECT = NULL;
		server_expect = realloc (server_expect, sizeof (char *) * (++server_expect_count));
		asprintf (&server_expect[server_expect_count - 1], "200");
		server_expect = realloc (server_expect, sizeof (char *) * (++server_expect_count));
		asprintf (&server_expect[server_expect_count - 1], "201");
		asprintf (&QUIT, "QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 119;
	}
	else {
		progname = strdup ("check_tcp");
		usage (_("CRITICAL - Generic check_tcp called with unknown service\n"));
	}

	server_address = strdup ("127.0.0.1");
	server_port = PORT;
	server_send = SEND;
	server_quit = QUIT;
	status = strdup ("");

	if (process_arguments (argc, argv) != TRUE)
		usage4 (_("Could not parse arguments"));

	/* use default expect if none listed in process_arguments() */
	if (EXPECT && server_expect_count == 0) {
		server_expect = malloc (sizeof (char *) * (++server_expect_count));
		server_expect[server_expect_count - 1] = EXPECT;
	}

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	gettimeofday (&tv, NULL);
#ifdef HAVE_SSL
	if (use_ssl && check_cert == TRUE) {
	  if (connect_SSL () != OK)
	    die (STATE_CRITICAL,"CRITICAL - Could not make SSL connection\n");
	  if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
	    result = check_certificate (&server_cert);
	    X509_free(server_cert);
	  }
	  else {
	    printf("CRITICAL - Cannot retrieve server certificate.\n");
	    result = STATE_CRITICAL;
	  }
	  SSL_shutdown (ssl);
	  SSL_free (ssl);
	  SSL_CTX_free (ctx);
	  close (sd);
	  return result;
	}
	else if (use_ssl)
		result = connect_SSL ();
	else
#endif
		{
			if (PROTOCOL == UDP_PROTOCOL)
				result = my_udp_connect (server_address, server_port, &sd);
			else
				/* default is TCP */
				result = my_tcp_connect (server_address, server_port, &sd);
		}

	if (result == STATE_CRITICAL)
		return STATE_CRITICAL;

	if (server_send != NULL) {		/* Something to send? */
		asprintf (&server_send, "%s\r\n", server_send);
#ifdef HAVE_SSL
		if (use_ssl)
			SSL_write(ssl, server_send, (int)strlen(server_send));
		else
#endif
			send (sd, server_send, strlen(server_send), 0);
	}

	if (delay > 0) {
		tv.tv_sec += delay;
		sleep (delay);
	}

	if (server_send || server_expect_count > 0) {

		buffer = malloc (MAXBUF);
		memset (buffer, '\0', MAXBUF);
		/* watch for the expect string */
		while ((i = my_recv ()) > 0) {
			buffer[i] = '\0';
			asprintf (&status, "%s%s", status, buffer);
			if (buffer[i-2] == '\r' && buffer[i-1] == '\n')
				break;
			if (maxbytes>0 && strlen(status) >= (unsigned)maxbytes)
				break;
		}

		/* return a CRITICAL status if we couldn't read any data */
		if (strlen(status) == 0)
			die (STATE_CRITICAL, _("No data received from host\n"));

		strip (status);

		if (status && verbose)
			printf ("%s\n", status);

		if (server_expect_count > 0) {
			for (i = 0;; i++) {
				if (verbose)
					printf ("%d %d\n", i, (int)server_expect_count);
				if (i >= (int)server_expect_count)
					die (STATE_WARNING, _("Invalid response from host\n"));
				if (strstr (status, server_expect[i]))
					break;
			}
		}
	}

	if (server_quit != NULL) {
#ifdef HAVE_SSL
		if (use_ssl) {
			SSL_write (ssl, server_quit, (int)strlen(server_quit));
			SSL_shutdown (ssl);
 			SSL_free (ssl);
 			SSL_CTX_free (ctx);
		}
		else {
#endif
			send (sd, server_quit, strlen (server_quit), 0);
#ifdef HAVE_SSL
		}
#endif
	}

	/* close the connection */
	if (sd)
		close (sd);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (check_critical_time == TRUE && elapsed_time > critical_time)
		result = STATE_CRITICAL;
	else if (check_warning_time == TRUE && elapsed_time > warning_time)
		result = STATE_WARNING;

	/* reset the alarm */
	alarm (0);

	printf
		(_("%s %s%s - %.3f second response time on port %d"),
		 SERVICE,
		 state_text (result),
		 (was_refused) ? " (refused)" : "",
		 elapsed_time, server_port);

	if (hide_output == FALSE && status && strlen(status) > 0)
		printf (" [%s]", status);

	printf (" |%s\n", fperfdata ("time", elapsed_time, "s",
		TRUE, warning_time,
		TRUE, critical_time,
		TRUE, 0,
		TRUE, socket_timeout));

	return result;
}



/* process command-line arguments */
int
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
		usage ("No arguments found\n");

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
		c = getopt_long (argc, argv, "+hVv46H:s:e:q:m:c:w:t:p:C:W:d:Sr:j:D",
		                 longopts, &option);

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':                 /* print short usage statement if args not parsable */
			printf (_("%s: Unknown argument: %s\n\n"), progname, optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':                 /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':                 /* version */
			print_revision (progname, "$Revision$");
			exit (STATE_OK);
		case 'v':                 /* verbose mode */
			verbose = TRUE;
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage (_("IPv6 support not available\n"));
#endif
			break;
		case 'H':                 /* hostname */
			if (is_host (optarg) == FALSE)
				usage2 (_("invalid hostname/address"), optarg);
			server_address = optarg;
			break;
		case 'c':                 /* critical */
			if (!is_intnonneg (optarg))
				usage (_("Critical threshold must be a positive integer\n"));
			else
				critical_time = strtod (optarg, NULL);
			check_critical_time = TRUE;
			break;
		case 'j':		  /* hide output */
			hide_output = TRUE;
			break;
		case 'w':                 /* warning */
			if (!is_intnonneg (optarg))
				usage (_("Warning threshold must be a positive integer\n"));
			else
				warning_time = strtod (optarg, NULL);
			check_warning_time = TRUE;
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
				usage (_("Timeout interval must be a positive integer\n"));
			else
				socket_timeout = atoi (optarg);
			break;
		case 'p':                 /* port */
			if (!is_intpos (optarg))
				usage (_("Port must be a positive integer\n"));
			else
				server_port = atoi (optarg);
			break;
		case 's':
			server_send = optarg;
			break;
		case 'e': /* expect string (may be repeated) */
			EXPECT = NULL;
			if (server_expect_count == 0)
				server_expect = malloc (sizeof (char *) * (++server_expect_count));
			else
				server_expect = realloc (server_expect, sizeof (char *) * (++server_expect_count));
			server_expect[server_expect_count - 1] = optarg;
			break;
		case 'm':
			if (!is_intpos (optarg))
				usage (_("Maxbytes must be a positive integer\n"));
			else
				maxbytes = atoi (optarg);
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
				usage (_("Refuse must be one of ok, warn, crit\n"));
			break;
		case 'd':
			if (is_intpos (optarg))
				delay = atoi (optarg);
			else
				usage (_("Delay must be a positive integer\n"));
			break;
                 case 'D': /* Check SSL cert validity - days 'til certificate expiration */
#ifdef HAVE_SSL
			if (!is_intnonneg (optarg))
				usage2 ("invalid certificate expiration period", optarg);
			days_till_exp = atoi (optarg);
			check_cert = TRUE;
			use_ssl = TRUE;
			break;
		case 'S':
			use_ssl = TRUE;
#else
			die (STATE_UNKNOWN, "SSL support not available.  Install OpenSSL and recompile.");
#endif
			break;
		}
	}

	if (server_address == NULL)
		usage (_("You must provide a server address\n"));

	return OK;
}



#ifdef HAVE_SSL
int
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
        ERR_print_errors_fp (stdout);
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
#endif



#ifdef HAVE_SSL
int
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
      printf ("CRITICAL - Wrong time format in certificate.\n");
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
      printf ("CRITICAL - Wrong time format in certificate.\n");
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
	  printf ("Certificate expires in %d day(s) (%s).\n", days_left, timestamp);
	  return STATE_WARNING;
        }
        if (days_left < 0) {
	  printf ("Certificate expired on %s.\n", timestamp);
	  return STATE_CRITICAL;
        }

        if (days_left == 0) {
	  printf ("Certificate expires today (%s).\n", timestamp);
	  return STATE_WARNING;
        }

        printf ("Certificate will expire on %s.\n", timestamp);

        return STATE_OK;
}
#endif



int
my_recv (void)
{
	int i;

#ifdef HAVE_SSL
	if (use_ssl) {
		i = SSL_read (ssl, buffer, MAXBUF - 1);
	}
	else {
#endif
		i = read (sd, buffer, MAXBUF - 1);
#ifdef HAVE_SSL
	}
#endif

	return i;
}



void
print_help (void)
{
	print_revision (progname, revision);

	printf (_("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n"));
	printf (_(COPYRIGHT), copyright, email);

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
	printf (_("\
Usage: %s -H host -p port [-w <warning time>] [-c <critical time>]\n\
  [-s <send string>] [-e <expect string>] [-q <quit string>]\n\
  [-m <maximum bytes>] [-d <delay>] [-t <timeout seconds>]\n\
  [-r <refuse state>] [-v] [-4|-6] [-j] [-D <days to cert expiry>]\n\
  [-S <use SSL>]\n"), progname);
	printf ("       %s (-h|--help)\n", progname);
	printf ("       %s (-V|--version)\n", progname);
}
