/******************************************************************************
 *
 * This file is part of the Nagios Plugins.
 *
 * Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>
 *
 * The Nagios Plugins are free software; you can redistribute them
 * and/or modify them under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 *
 *****************************************************************************/

#define PROGRAM check_tcp
#define DESCRIPTION "Check a TCP port"
#define AUTHOR "Ethan Galstad"
#define EMAIL "nagios@nagios.org"
#define COPYRIGHTDATE "1999"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#ifdef HAVE_SSL_H
#include <rsa.h>
#include <crypto.h>
#include <x509.h>
#include <pem.h>
#include <ssl.h>
#include <err.h>
#endif

#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#ifdef HAVE_SSL
SSL_CTX *ctx;
SSL *ssl;
int connect_SSL (void);
#endif

#define TCP_PROTOCOL 1
#define UDP_PROTOCOL 2

int process_arguments (int, char **);
void print_usage (void);
void print_help (void);

char *PROGNAME = NULL;
char *SERVICE = NULL;
char *SEND = NULL;
char *EXPECT = NULL;
char *QUIT = NULL;
int PROTOCOL = 0;
int PORT = 0;

int server_port = 0;
char *server_address = NULL;
char *server_send = NULL;
char *server_quit = NULL;
char **server_expect = NULL;
int server_expect_count = 0;
char **warn_codes = NULL;
int warn_codes_count = 0;
char **crit_codes = NULL;
int crit_codes_count = 0;
int delay = 0;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
int verbose = FALSE;
int use_ssl = FALSE;
int sd;

int
main (int argc, char **argv)
{
	int result;
	int i;
	char buffer[MAX_INPUT_BUFFER] = "";
	char *status = NULL;
	char *output = NULL;
	char *ptr = NULL;

	if (strstr (argv[0], "check_udp")) {
		PROGNAME = strscpy (PROGNAME, "check_udp");
		SERVICE = strscpy (SERVICE, "UDP");
		SEND = NULL;
		EXPECT = NULL;
		QUIT = NULL;
		PROTOCOL = UDP_PROTOCOL;
		PORT = 0;
	}
	else if (strstr (argv[0], "check_tcp")) {
		PROGNAME = strscpy (PROGNAME, "check_tcp");
		SERVICE = strscpy (SERVICE, "TCP");
		SEND = NULL;
		EXPECT = NULL;
		QUIT = NULL;
		PROTOCOL = TCP_PROTOCOL;
		PORT = 0;
	}
	else if (strstr (argv[0], "check_ftp")) {
		PROGNAME = strscpy (PROGNAME, "check_ftp");
		SERVICE = strscpy (SERVICE, "FTP");
		SEND = NULL;
		EXPECT = strscpy (EXPECT, "220");
		QUIT = strscpy (QUIT, "QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 21;
	}
	else if (strstr (argv[0], "check_smtp")) {
		PROGNAME = strscpy (PROGNAME, "check_smtp");
		SERVICE = strscpy (SERVICE, "SMTP");
		SEND = NULL;
		EXPECT = strscpy (EXPECT, "220");
		QUIT = strscpy (QUIT, "QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 25;
	}
	else if (strstr (argv[0], "check_pop")) {
		PROGNAME = strscpy (PROGNAME, "check_pop");
		SERVICE = strscpy (SERVICE, "POP");
		SEND = NULL;
		EXPECT = strscpy (EXPECT, "110");
		QUIT = strscpy (QUIT, "QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 110;
	}
	else if (strstr (argv[0], "check_imap")) {
		PROGNAME = strscpy (PROGNAME, "check_imap");
		SERVICE = strscpy (SERVICE, "IMAP");
		SEND = NULL;
		EXPECT = strscpy (EXPECT, "* OK");
		QUIT = strscpy (QUIT, "a1 LOGOUT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 143;
	}
#ifdef HAVE_SSL
	else if (strstr(argv[0],"check_simap")) {
		PROGNAME=strscpy(PROGNAME,"check_simap");
		SERVICE=strscpy(SERVICE,"SIMAP");
		SEND=NULL;
		EXPECT=strscpy(EXPECT,"* OK");
		QUIT=strscpy(QUIT,"a1 LOGOUT\n");
		PROTOCOL=TCP_PROTOCOL;
		use_ssl=TRUE;
		PORT=993;
	}
#endif
	else if (strstr (argv[0], "check_nntp")) {
		PROGNAME = strscpy (PROGNAME, "check_nntp");
		SERVICE = strscpy (SERVICE, "NNTP");
		SEND = NULL;
		EXPECT = NULL;
		server_expect = realloc (server_expect, ++server_expect_count);
		server_expect[server_expect_count - 1] = strscpy (EXPECT, "200");
		server_expect = realloc (server_expect, ++server_expect_count);
		server_expect[server_expect_count - 1] = strscpy (NULL, "201");
		QUIT = strscpy (QUIT, "QUIT\r\n");
		PROTOCOL = TCP_PROTOCOL;
		PORT = 119;
	}
	else {
		usage ("ERROR: Generic check_tcp called with unknown service\n");
	}

	server_address = strscpy (NULL, "127.0.0.1");
	server_port = PORT;
	server_send = SEND;
	server_quit = QUIT;

	if (process_arguments (argc, argv) == ERROR)
		usage ("Could not parse arguments\n");

	/* use default expect if none listed in process_arguments() */
	if (EXPECT && server_expect_count == 0) {
		server_expect = malloc (1);
		server_expect[server_expect_count - 1] = EXPECT;
	}

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* try to connect to the host at the given port number */
	time (&start_time);
#ifdef HAVE_SSL
	if (use_ssl)
		result = connect_SSL ();
	else
#endif
		{
			if (PROTOCOL == UDP_PROTOCOL)
				result = my_udp_connect (server_address, server_port, &sd);
			else													/* default is TCP */
				result = my_tcp_connect (server_address, server_port, &sd);
		}

	if (result == STATE_CRITICAL)
		return STATE_CRITICAL;

	if (server_send != NULL) {		/* Something to send? */
		snprintf (buffer, MAX_INPUT_BUFFER - 1, "%s\r\n", server_send);
		buffer[MAX_INPUT_BUFFER - 1] = 0;
#ifdef HAVE_SSL
		if (use_ssl)
			SSL_write(ssl,buffer,strlen(buffer));
		else
#endif
			send (sd, buffer, strlen (buffer), 0);
	}

	if (delay > 0) {
		start_time = start_time + delay;
		sleep (delay);
	}

	if (server_send || server_expect_count > 0) {

		asprintf (&status, "");

		/* watch for the expect string */
#ifdef HAVE_SSL
		if (use_ssl && SSL_read (ssl, buffer, MAX_INPUT_BUFFER - 1)>=0)
			asprintf (&status, "%s%s", status, buffer);
		else
#endif
			{
				if (recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0) >= 0)
					asprintf (&status, "%s%s", status, buffer);
			}
		strip (status);

		/* return a CRITICAL status if we couldn't read any data */
		if (status == NULL)
			terminate (STATE_CRITICAL, "No data received from host\n");

		if (status && verbose)
			printf ("%s\n", status);

		if (server_expect_count > 0) {
			for (i = 0;; i++) {
				if (verbose)
					printf ("%d %d\n", i, server_expect_count);
				if (i >= server_expect_count)
					terminate (STATE_WARNING, "Invalid response from host\n");
				if (strstr (status, server_expect[i]))
					break;
			}
		}
	}

	if (server_quit)
#ifdef HAVE_SSL
		if (use_ssl) {
			SSL_write (ssl, QUIT, strlen (QUIT));
			SSL_shutdown (ssl);
			SSL_free (ssl);
			SSL_CTX_free (ctx);
		}
		else
#endif
  		send (sd, server_quit, strlen (server_quit), 0);

	/* close the connection */
	close (sd);

	time (&end_time);

	if (check_critical_time == TRUE && (end_time - start_time) > critical_time)
		result = STATE_CRITICAL;
	else if (check_warning_time == TRUE
					 && (end_time - start_time) > warning_time) result = STATE_WARNING;

	/* reset the alarm */
	alarm (0);

	printf
		("%s %s - %d second response time on port %d",
		 SERVICE,
		 state_text (result), (int) (end_time - start_time), server_port);

	if (status)
		printf (" [%s]", status);

	printf ("|time=%d\n", (int) (end_time - start_time));

	return result;
}







/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
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
		{"quit", required_argument, 0, 'q'},
		{"delay", required_argument, 0, 'd'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};
#endif

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
#ifdef HAVE_GETOPT_H
		c =
			getopt_long (argc, argv, "+hVvH:s:e:q:c:w:t:p:C:W:d:S", long_options,
									 &option_index);
#else
		c = getopt (argc, argv, "+hVvH:s:e:q:c:w:t:p:C:W:d:S");
#endif

		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case '?':                 /* print short usage statement if args not parsable */
			printf ("%s: Unknown argument: %s\n\n", my_basename (argv[0]), optarg);
			print_usage ();
			exit (STATE_UNKNOWN);
		case 'h':                 /* help */
			print_help ();
			exit (STATE_OK);
		case 'V':                 /* version */
			print_revision (PROGNAME, "$Revision$");
			exit (STATE_OK);
		case 'v':                 /* verbose mode */
			verbose = TRUE;
			break;
		case 'H':                 /* hostname */
			if (is_host (optarg) == FALSE)
				usage ("Invalid host name/address\n");
			server_address = optarg;
			break;
		case 'c':                 /* critical */
			if (!is_intnonneg (optarg))
				usage ("Critical threshold must be a nonnegative integer\n");
			critical_time = atoi (optarg);
			check_critical_time = TRUE;
			break;
		case 'w':                 /* warning */
			if (!is_intnonneg (optarg))
				usage ("Warning threshold must be a nonnegative integer\n");
			warning_time = atoi (optarg);
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
				usage ("Timeout interval must be a positive integer\n");
			socket_timeout = atoi (optarg);
			break;
		case 'p':                 /* port */
			if (!is_intpos (optarg))
				usage ("Server port must be a positive integer\n");
			server_port = atoi (optarg);
			break;
		case 's':
			server_send = optarg;
			break;
		case 'e': /* expect string (may be repeated) */
			EXPECT = NULL;
			if (server_expect_count == 0)
				server_expect = malloc (++server_expect_count);
			else
				server_expect = realloc (server_expect, ++server_expect_count);
			server_expect[server_expect_count - 1] = optarg;
			break;
		case 'q':
			server_quit = optarg;
			break;
		case 'd':
			if (is_intpos (optarg))
				delay = atoi (optarg);
			else
				usage ("Delay must be a positive integer\n");
			break;
		case 'S':
#ifndef HAVE_SSL
			terminate (STATE_UNKNOWN,
				"SSL support not available. Install OpenSSL and recompile.");
#endif
			use_ssl = TRUE;
			break;
		}
	}

	if (server_address == NULL)
		usage ("You must provide a server address\n");

	return OK;
}





void
print_usage (void)
{
	printf
		("Usage: %s -H host -p port [-w warn_time] [-c crit_time] [-s send]\n"
		 "         [-e expect] [-W wait] [-t to_sec] [-v]\n", PROGNAME);
}





void
print_help (void)
{
	print_revision (PROGNAME, "$Revision$");
	printf
		("Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)\n\n"
		 "This plugin tests %s connections with the specified host.\n\n",
		 SERVICE);
	print_usage ();
	printf
		("Options:\n"
		 " -H, --hostname=ADDRESS\n"
		 "    Host name argument for servers using host headers (use numeric\n"
		 "    address if possible to bypass DNS lookup).\n"
		 " -p, --port=INTEGER\n"
		 "    Port number\n"
		 " -s, --send=STRING\n"
		 "    String to send to the server\n"
		 " -e, --expect=STRING\n"
		 "    String to expect in server response"
		 " -W, --wait=INTEGER\n"
		 "    Seconds to wait between sending string and polling for response\n"
		 " -w, --warning=INTEGER\n"
		 "    Response time to result in warning status (seconds)\n"
		 " -c, --critical=INTEGER\n"
		 "    Response time to result in critical status (seconds)\n"
		 " -t, --timeout=INTEGER\n"
		 "    Seconds before connection times out (default: %d)\n"
		 " -v"
		 "    Show details for command-line debugging (do not use with nagios server)\n"
		 " -h, --help\n"
		 "    Print detailed help screen\n"
		 " -V, --version\n"
		 "    Print version information\n", DEFAULT_SOCKET_TIMEOUT);
}


#ifdef HAVE_SSL
int
connect_SSL (void)
{
  SSL_METHOD *meth;

  /* Initialize SSL context */
  SSLeay_add_ssl_algorithms ();
  meth = SSLv2_client_method ();
  SSL_load_error_strings ();
  if ((ctx = SSL_CTX_new (meth)) == NULL)
    {
      printf ("ERROR: Cannot create SSL context.\n");
      return STATE_CRITICAL;
    }

  /* Initialize alarm signal handling */
  signal (SIGALRM, socket_timeout_alarm_handler);

  /* Set socket timeout */
  alarm (socket_timeout);

  /* Save start time */
  time (&start_time);

  /* Make TCP connection */
  if (my_tcp_connect (server_address, server_port, &sd) == STATE_OK)
    {
    /* Do the SSL handshake */
      if ((ssl = SSL_new (ctx)) != NULL)
      {
        SSL_set_fd (ssl, sd);
        if (SSL_connect (ssl) != -1)
          return OK;
        ERR_print_errors_fp (stderr);
      }
      else
      {
        printf ("ERROR: Cannot initiate SSL handshake.\n");
      }
      SSL_free (ssl);
    }

  SSL_CTX_free (ctx);
  close (sd);

  return STATE_CRITICAL;
}
#endif

