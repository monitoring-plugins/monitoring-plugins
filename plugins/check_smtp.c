/******************************************************************************

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
 
******************************************************************************/

const char *progname = "check_smtp";
const char *revision = "$Revision$";
const char *copyright = "2000-2004";
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
SSL_CTX *ctx;
SSL *ssl;
X509 *server_cert;
int connect_STARTTLS (void);
int check_certificate (X509 **);
#endif

enum {
	SMTP_PORT	= 25
};
#define SMTP_EXPECT "220"
#define SMTP_HELO "HELO "
#define SMTP_EHLO "EHLO "
#define SMTP_QUIT "QUIT\r\n"
#define SMTP_STARTTLS "STARTTLS\r\n"

#ifndef HOST_MAX_BYTES
#define HOST_MAX_BYTES 255
#endif

#define EHLO_SUPPORTS_STARTTLS 1

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);
int myrecv(void);
int my_close(void);

#ifdef HAVE_REGEX_H
#include <regex.h>
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char timestamp[20] = "";
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;
#endif

int server_port = SMTP_PORT;
char *server_address = NULL;
char *server_expect = NULL;
int smtp_use_dummycmd = 0;
char *mail_command = NULL;
char *from_arg = NULL;
int ncommands=0;
int command_size=0;
int nresponses=0;
int response_size=0;
char **commands = NULL;
char **responses = NULL;
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
int verbose = 0;
int use_ssl = FALSE;
short use_ehlo = FALSE;
short ssl_established = TRUE;
char *localhostname = NULL;
int sd;
char buffer[MAX_INPUT_BUFFER];
enum {
  TCP_PROTOCOL = 1,
  UDP_PROTOCOL = 2,
  MAXBUF = 1024
};

int
main (int argc, char **argv)
{
	short supports_tls=FALSE;
	int n = 0;
	double elapsed_time;
	long microsec;
	int result = STATE_UNKNOWN;
	char *cmd_str = NULL;
	char *helocmd = NULL;
	struct timeval tv;
	struct hostent *hp;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize the HELO command with the localhostname */
	if(! localhostname){
		localhostname = malloc (HOST_MAX_BYTES);
		if(!localhostname){
			printf(_("malloc() failed!\n"));
			return STATE_CRITICAL;
		}
		if(gethostname(localhostname, HOST_MAX_BYTES)){
			printf(_("gethostname() failed!\n"));
			return STATE_CRITICAL;
		}
		hp = gethostbyname(localhostname);
		if(!hp) helocmd = localhostname;
		else helocmd = hp->h_name;
	} else {
		helocmd = localhostname;
	}
	if(use_ehlo)
		asprintf (&helocmd, "%s%s%s", SMTP_EHLO, helocmd, "\r\n");
	else
		asprintf (&helocmd, "%s%s%s", SMTP_HELO, helocmd, "\r\n");

	/* initialize the MAIL command with optional FROM command  */
	asprintf (&cmd_str, "%sFROM: %s%s", mail_command, from_arg, "\r\n");

	if (verbose && smtp_use_dummycmd)
		printf ("FROM CMD: %s", cmd_str);
	
	/* initialize alarm signal handling */
	(void) signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	(void) alarm (socket_timeout);

	/* start timer */
	gettimeofday (&tv, NULL);

	/* try to connect to the host at the given port number */
	result = my_tcp_connect (server_address, server_port, &sd);

	if (result == STATE_OK) { /* we connected */

		/* watch for the SMTP connection string and */
		/* return a WARNING status if we couldn't read any data */
		if (recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0) == -1) {
			printf (_("recv() failed\n"));
			result = STATE_WARNING;
		}
		else {
			if (verbose)
				printf ("%s", buffer);
			/* strip the buffer of carriage returns */
			strip (buffer);
			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {
				if (server_port == SMTP_PORT)
					printf (_("Invalid SMTP response received from host\n"));
				else
					printf (_("Invalid SMTP response received from host on port %d\n"),
									server_port);
				result = STATE_WARNING;
			}
		}

		/* send the HELO/EHLO command */
		send(sd, helocmd, strlen(helocmd), 0);

		/* allow for response to helo command to reach us */
		if(read (sd, buffer, MAXBUF - 1) < 0){
			printf (_("recv() failed\n"));
			return STATE_WARNING;
		} else if(use_ehlo){
			buffer[MAXBUF-1]='\0';
			if(strstr(buffer, "250 STARTTLS") != NULL ||
			   strstr(buffer, "250-STARTTLS") != NULL){
				supports_tls=TRUE;
			}
		}

		if(use_ssl && ! supports_tls){
			printf(_("WARNING - TLS not supported by server\n"));
			send (sd, SMTP_QUIT, strlen (SMTP_QUIT), 0);
			return STATE_WARNING;
		}

#ifdef HAVE_SSL
		if(use_ssl) {
		  /* send the STARTTLS command */
		  send(sd, SMTP_STARTTLS, strlen(SMTP_STARTTLS), 0);

		  recv(sd,buffer, MAX_INPUT_BUFFER-1, 0); /* wait for it */
		  if (!strstr (buffer, server_expect)) {
		    printf (_("Server does not support STARTTLS\n"));
		    send (sd, SMTP_QUIT, strlen (SMTP_QUIT), 0);
		    return STATE_UNKNOWN;
		  }
		  if(connect_STARTTLS() != OK) {
		    printf (_("CRITICAL - Cannot create SSL context.\n"));
		    return STATE_CRITICAL;
		  } else {
			ssl_established = TRUE;
		  }
		  if ( check_cert ) {
		    if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
		      result = check_certificate (&server_cert);
		      X509_free(server_cert);
		    }
		    else {
		      printf (_("CRITICAL - Cannot retrieve server certificate.\n"));
		      result = STATE_CRITICAL;
			      
		    }
		    my_close();
		    return result;
		  }
		}
#endif
				
		/* sendmail will syslog a "NOQUEUE" error if session does not attempt
		 * to do something useful. This can be prevented by giving a command
		 * even if syntax is illegal (MAIL requires a FROM:<...> argument)
		 *
		 * According to rfc821 you can include a null reversepath in the from command
		 * - but a log message is generated on the smtp server.
		 *
		 * You can disable sending mail_command with '--nocommand'
		 * Use the -f option to provide a FROM address
		 */
		if (smtp_use_dummycmd) {
#ifdef HAVE_SSL
		  if (use_ssl)
		    SSL_write(ssl, cmd_str, strlen(cmd_str));
		  else
#endif
		  send(sd, cmd_str, strlen(cmd_str), 0);
		  myrecv();
		  if (verbose) 
		    printf("%s", buffer);
		}

		while (n < ncommands) {
			asprintf (&cmd_str, "%s%s", commands[n], "\r\n");
#ifdef HAVE_SSL
			if (use_ssl)
			  SSL_write(ssl,cmd_str, strlen(cmd_str));
			else
#endif
			send(sd, cmd_str, strlen(cmd_str), 0);
			myrecv();
			if (verbose) 
				printf("%s", buffer);
			strip (buffer);
			if (n < nresponses) {
#ifdef HAVE_REGEX_H
				cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
				errcode = regcomp (&preg, responses[n], cflags);
				if (errcode != 0) {
					regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Could Not Compile Regular Expression"));
					return ERROR;
				}
				excode = regexec (&preg, buffer, 10, pmatch, eflags);
				if (excode == 0) {
					result = STATE_OK;
				}
				else if (excode == REG_NOMATCH) {
					result = STATE_WARNING;
					printf (_("SMTP %s - Invalid response '%s' to command '%s'\n"), state_text (result), buffer, commands[n]);
				}
				else {
					regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Execute Error: %s\n"), errbuf);
					result = STATE_UNKNOWN;
				}
#else
				if (strstr(buffer, responses[n])!=buffer) {
					result = STATE_WARNING;
					printf (_("SMTP %s - Invalid response '%s' to command '%s'\n"), state_text (result), buffer, commands[n]);
				}
#endif
			}
			n++;
		}

		/* tell the server we're done */
#ifdef HAVE_SSL
		if (use_ssl)
		  SSL_write(ssl,SMTP_QUIT, strlen (SMTP_QUIT));
		else
#endif
		send (sd, SMTP_QUIT, strlen (SMTP_QUIT), 0);

		/* finally close the connection */
		close (sd);
	}

	/* reset the alarm */
	alarm (0);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (result == STATE_OK) {
		if (check_critical_time && elapsed_time > (double) critical_time)
			result = STATE_CRITICAL;
		else if (check_warning_time && elapsed_time > (double) warning_time)
			result = STATE_WARNING;
	}

	printf (_("SMTP %s - %.3f sec. response time%s%s|%s\n"),
	        state_text (result), elapsed_time,
          verbose?", ":"", verbose?buffer:"",
	        fperfdata ("time", elapsed_time, "s",
	                  (int)check_warning_time, warning_time,
	                  (int)check_critical_time, critical_time,
	                  TRUE, 0, FALSE, 0));

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
		{"expect", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"from", required_argument, 0, 'f'},
		{"fqdn", required_argument, 0, 'F'},
		{"command", required_argument, 0, 'C'},
		{"response", required_argument, 0, 'R'},
		{"nocommand", required_argument, 0, 'n'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"help", no_argument, 0, 'h'},
		{"starttls",no_argument,0,'S'},
		{"certificate",required_argument,0,'D'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVv46t:p:f:e:c:w:H:C:R:SD:F:",
		                 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'p':									/* port */
			if (is_intpos (optarg))
				server_port = atoi (optarg);
			else
				usage4 (_("Port must be a positive integer"));
			break;
		case 'F':
		/* localhostname */
			localhostname = strdup(optarg);
			break;
		case 'f':									/* from argument */
			from_arg = optarg;
			smtp_use_dummycmd = 1;
			break;
		case 'e':									/* server expect string on 220  */
			server_expect = optarg;
			break;
		case 'C':									/* commands  */
			if (ncommands >= command_size) {
				commands = realloc (commands, command_size+8);
				if (commands == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), ncommands);
			}
			commands[ncommands] = optarg;
			ncommands++;
			break;
		case 'R':									/* server responses */
			if (nresponses >= response_size) {
				responses = realloc (responses, response_size+8);
				if (responses == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), nresponses);
			}
			responses[nresponses] = optarg;
			nresponses++;
			break;
		case 'c':									/* critical time threshold */
			if (is_intnonneg (optarg)) {
				critical_time = atoi (optarg);
				check_critical_time = TRUE;
			}
			else {
				usage4 (_("Critical time must be a positive integer"));
			}
			break;
		case 'w':									/* warning time threshold */
			if (is_intnonneg (optarg)) {
				warning_time = atoi (optarg);
				check_warning_time = TRUE;
			}
			else {
				usage4 (_("Warning time must be a positive integer"));
			}
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				socket_timeout = atoi (optarg);
			}
			else {
				usage4 (_("Timeout interval must be a positive integer"));
			}
			break;
		case 'S':
		/* starttls */
			use_ssl = TRUE;
			use_ehlo = TRUE;
			break;
		case 'D':
		/* Check SSL cert validity */
#ifdef HAVE_SSL
			if (!is_intnonneg (optarg))
				usage2 ("Invalid certificate expiration period",optarg);
				days_till_exp = atoi (optarg);
				check_cert = TRUE;
#else
				usage (_("SSL support not available - install OpenSSL and recompile"));
#endif
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
		case 'V':									/* version */
			print_revision (progname, revision);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage2 (_("Unknown argument"), optarg);
		}
	}

	c = optind;
	if (server_address == NULL) {
		if (argv[c]) {
			if (is_host (argv[c]))
				server_address = argv[c];
			else
				usage2 (_("Invalid hostname/address"), argv[c]);
		}
		else {
			asprintf (&server_address, "127.0.0.1");
		}
	}

	if (server_expect == NULL)
		server_expect = strdup (SMTP_EXPECT);

	if (mail_command == NULL)
		mail_command = strdup("MAIL ");

	if (from_arg==NULL)
		from_arg = strdup(" ");

	return validate_arguments ();
}



int
validate_arguments (void)
{
	return OK;
}



void
print_help (void)
{
	char *myport;
	asprintf (&myport, "%d", SMTP_PORT);

	print_revision (progname, revision);

	printf ("Copyright (c) 1999-2001 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf(_("This plugin will attempt to open an SMTP connection with the host.\n\n"));

	print_usage ();

	printf (_(UT_HELP_VRSN));

	printf (_(UT_HOST_PORT), 'p', myport);

	printf (_(UT_IPv46));

	printf (_("\
 -e, --expect=STRING\n\
   String to expect in first line of server response (default: '%s')\n\
 -n, nocommand\n\
   Suppress SMTP command\n\
 -C, --command=STRING\n\
   SMTP command (may be used repeatedly)\n\
 -R, --command=STRING\n\
   Expected response to command (may be used repeatedly)\n\
 -f, --from=STRING\n\
   FROM-address to include in MAIL command, required by Exchange 2000\n"),
	        SMTP_EXPECT);
#ifdef HAVE_SSL
        printf (_("\
 -D, --certificate=INTEGER\n\
    Minimum number of days a certificate has to be valid.\n\
 -S, --starttls\n\
    Use STARTTLS for the connection.\n"));
#endif

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

	printf(_("\n\
Successul connects return STATE_OK, refusals and timeouts return\n\
STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful\n\
connects, but incorrect reponse messages from the host result in\n\
STATE_WARNING return values.\n"));

	printf (_(UT_SUPPORT));
}



void
print_usage (void)
{
	printf ("\
Usage: %s -H host [-p port] [-e expect] [-C command] [-f from addr]\n\
                  [-w warn] [-c crit] [-t timeout] [-S] [-D days] [-n] [-v] [-4|-6]\n", progname);
}

#ifdef HAVE_SSL
int
connect_STARTTLS (void)
{
  SSL_METHOD *meth;

  /* Initialize SSL context */
  SSLeay_add_ssl_algorithms ();
  meth = SSLv23_client_method ();
  SSL_load_error_strings ();
  if ((ctx = SSL_CTX_new (meth)) == NULL)
    {
      printf(_("CRITICAL - Cannot create SSL context.\n"));
      return STATE_CRITICAL;
    }
  /* do the SSL handshake */
  if ((ssl = SSL_new (ctx)) != NULL)
    {
      SSL_set_fd (ssl, sd);
      /* original version checked for -1
	 I look for success instead (1) */
      if (SSL_connect (ssl) == 1)
	return OK;
      ERR_print_errors_fp (stderr);
    }
  else
    {
      printf (_("CRITICAL - Cannot initiate SSL handshake.\n"));
    }
  my_close();
  
  return STATE_CRITICAL;
}

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
    (timestamp, sizeof(timestamp), "%02d/%02d/%04d %02d:%02d",
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
myrecv (void)
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

int 
my_close (void)
{
#ifdef HAVE_SSL
  if (use_ssl == TRUE && ssl_established == TRUE) {
    SSL_shutdown (ssl);
    SSL_free (ssl);
    SSL_CTX_free (ctx);
    return 0;
  }
  else {
#endif
    return close(sd);
#ifdef HAVE_SSL
  }
#endif
}
