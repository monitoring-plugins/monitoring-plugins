/****************************************************************************
 *
 * Program: HTTP plugin for Nagios
 * License: GPL
 *
 * License Information:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * $Id$
 *
 *****************************************************************************/

#define PROGNAME "check_http"
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHORS "Ethan Galstad/Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"

#include "config.h"
#include "common.h"
#include "version.h"
#include "netutils.h"
#include "utils.h"

#define SUMMARY "\
This plugin tests the HTTP service on the specified host. It can test\n\
normal (http) and secure (https) servers, follow redirects, search for\n\
strings and regular expressions, check connection times, and report on\n\
certificate expiration times.\n"

#define OPTIONS "\
\(-H <vhost> | -I <IP-address>) [-u <uri>] [-p <port>]\n\
            [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L]\n\
            [-a auth] [-f <ok | warn | critcal | follow>] [-e <expect>]\n\
            [-s string] [-r <regex> | -R <case-insensitive regex>]\n\
            [-P string]"

#define LONGOPTIONS "\
 -H, --hostname=ADDRESS\n\
    Host name argument for servers using host headers (virtual host)\n\
 -I, --IP-address=ADDRESS\n\
   IP address or name (use numeric address if possible to bypass DNS lookup).\n\
 -e, --expect=STRING\n\
   String to expect in first (status) line of server response (default: %s)\n\
   If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)\n\
 -s, --string=STRING\n\
   String to expect in the content\n\
 -u, --url=PATH\n\
   URL to GET or POST (default: /)\n\
 -p, --port=INTEGER\n\
   Port number (default: %d)\n\
 -P, --post=STRING\n\
   URL encoded http POST data\n\
 -w, --warning=INTEGER\n\
   Response time to result in warning status (seconds)\n\
 -c, --critical=INTEGER\n\
   Response time to result in critical status (seconds)\n\
 -t, --timeout=INTEGER\n\
   Seconds before connection times out (default: %d)\n\
 -a, --authorization=AUTH_PAIR\n\
   Username:password on sites with basic authentication\n\
 -L, --link=URL\n\
   Wrap output in HTML link (obsoleted by urlize)\n\
 -f, --onredirect=<ok|warning|critical|follow>\n\
   How to handle redirected pages\n%s\
 -v, --verbose\n\
    Show details for command-line debugging (do not use with nagios server)\n\
 -h, --help\n\
    Print detailed help screen\n\
 -V, --version\n\
    Print version information\n"

#ifdef HAVE_SSL
#define SSLOPTIONS "\
 -S, --ssl\n\
    Connect via SSL\n\
 -C, --certificate=INTEGER\n\
    Minimum number of days a certificate has to be valid.\n\
    (when this option is used the url is not checked.)\n"
#else
#define SSLOPTIONS ""
#endif

#define DESCRIPTION "\
This plugin will attempt to open an HTTP connection with the host. Successul\n\
connects return STATE_OK, refusals and timeouts return STATE_CRITICAL, other\n\
errors return STATE_UNKNOWN.  Successful connects, but incorrect reponse\n\
messages from the host result in STATE_WARNING return values.  If you are\n\
checking a virtual server that uses \"host headers\" you must supply the FQDN\n\
\(fully qualified domain name) as the [host_name] argument.\n"

#define SSLDESCRIPTION "\
This plugin can also check whether an SSL enabled web server is able to\n\
serve content (optionally within a specified time) or whether the X509 \n\
certificate is still valid for the specified number of days.\n\n\
CHECK CONTENT: check_http -w 5 -c 10 --ssl www.verisign.com\n\n\
When the 'www.verisign.com' server returns its content within 5 seconds, a\n\
STATE_OK will be returned. When the server returns its content but exceeds\n\
the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,\n\
a STATE_CRITICAL will be returned.\n\n\
CHECK CERTIFICATE: check_http www.verisign.com -C 14\n\n\
When the certificate of 'www.verisign.com' is valid for more than 14 days, a\n\
STATE_OK is returned. When the certificate is still valid, but for less than\n\
14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when\n\
the certificate is expired.\n"

#ifdef HAVE_SSL_H
#include <rsa.h>
#include <crypto.h>
#include <x509.h>
#include <pem.h>
#include <ssl.h>
#include <err.h>
#include <rand.h>
#endif

#ifdef HAVE_OPENSSL_SSL_H
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#endif

#ifdef HAVE_SSL
int check_cert = FALSE;
int days_till_exp;
unsigned char *randbuff;
SSL_CTX *ctx;
SSL *ssl;
X509 *server_cert;
int connect_SSL (void);
int check_certificate (X509 **);
#endif

#ifdef HAVE_REGEX_H
#define REGS 2
#define MAX_RE_SIZE 256
#include <regex.h>
regex_t preg;
regmatch_t pmatch[REGS];
char regexp[MAX_RE_SIZE];
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
int errcode;
#endif

#define server_type_check(server_type) \
(strcmp (server_type, "https") ? FALSE : TRUE)

#define server_port_check(use_ssl) (use_ssl ? HTTPS_PORT : HTTP_PORT)

#define MAX_IPV4_HOSTLENGTH 64
#define HDR_LOCATION "%*[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]: "
#define URI_HTTP "%[HTPShtps]://"
#define URI_HOST "%[a-zA-Z0-9.-]"
#define URI_PORT ":%[0-9]"
#define URI_PATH "%[/a-zA-Z0-9._-=@,]"

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define HTTP_EXPECT "HTTP/1."
#define HTTP_URL "/"

char timestamp[17] = "";
int specify_port = FALSE;
int server_port = HTTP_PORT;
char server_port_text[6] = "";
char server_type[6] = "http";
/*@null@*/ char *server_address = NULL; 
/*@null@*/ char *host_name = NULL;
/*@null@*/ char *server_url = NULL;
int server_url_length = 0;
int server_expect_yn = 0;
char server_expect[MAX_INPUT_BUFFER] = HTTP_EXPECT;
char string_expect[MAX_INPUT_BUFFER] = "";
int warning_time = 0;
int check_warning_time = FALSE;
int critical_time = 0;
int check_critical_time = FALSE;
char user_auth[MAX_INPUT_BUFFER] = "";
int display_html = FALSE;
int onredirect = STATE_OK;
int use_ssl = FALSE;
int verbose = FALSE;
int sd;
/*@null@*/ char *http_method = NULL;
/*@null@*/ char *http_post_data = NULL;
char buffer[MAX_INPUT_BUFFER];

void print_usage (void);
void print_help (void);
int process_arguments (int, char **);
int call_getopt (int, char **);
static char *base64 (char *bin, int len);
int check_http (void);
int my_recv (void);
int my_close (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	if (process_arguments (argc, argv) == ERROR)
		usage ("check_http: could not parse arguments\n");

	if (strstr (timestamp, ":")) {
		if (strstr (server_url, "?"))
			asprintf (&server_url, "%s&%s", server_url, timestamp);
		else
			asprintf (&server_url, "%s?%s", server_url, timestamp);
	}

	if (display_html == TRUE)
		printf ("<A HREF=\"http://%s:%d%s\" target=\"_blank\">",
		        host_name, server_port, server_url);

	/* initialize alarm signal handling, set socket timeout, start timer */
	(void) signal (SIGALRM, socket_timeout_alarm_handler);
	(void) alarm (socket_timeout);
	(void) time (&start_time);

#ifdef HAVE_SSL
	if (use_ssl && check_cert == TRUE) {
		if (connect_SSL () != OK)
			terminate (STATE_CRITICAL,
			           "HTTP CRITICAL - Could not make SSL connection\n");
		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			result = check_certificate (&server_cert);
			X509_free (server_cert);
		}
		else {
			printf ("ERROR: Cannot retrieve server certificate.\n");
			result = STATE_CRITICAL;
		}
		SSL_shutdown (ssl);
		SSL_free (ssl);
		SSL_CTX_free (ctx);
		close (sd);
	}
	else {
		result = check_http ();
	}
#else
	result = check_http ();
#endif
	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c, i = 1;
	char optchars[MAX_INPUT_BUFFER];

#ifdef HAVE_GETOPT_H
	int option_index = 0;
	static struct option long_options[] = {
		STD_OPTS_LONG,
		{"link", no_argument, 0, 'L'},
		{"nohtml", no_argument, 0, 'n'},
		{"ssl", no_argument, 0, 'S'},
		{"verbose", no_argument, 0, 'v'},
		{"post", required_argument, 0, 'P'},
		{"IP-address", required_argument, 0, 'I'},
		{"string", required_argument, 0, 's'},
		{"regex", required_argument, 0, 'r'},
		{"ereg", required_argument, 0, 'r'},
		{"eregi", required_argument, 0, 'R'},
		{"onredirect", required_argument, 0, 'f'},
		{"certificate", required_argument, 0, 'C'},
		{0, 0, 0, 0}
	};
#endif

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		if (strcmp ("-hn", argv[c]) == 0)
			strcpy (argv[c], "-H");
		if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
		if (strcmp ("-nohtml", argv[c]) == 0)
			strcpy (argv[c], "-n");
	}

	snprintf (optchars, MAX_INPUT_BUFFER, "%s%s", STD_OPTS,
	          "P:I:a:e:p:s:R:r:u:f:C:nLS");

	while (1) {
#ifdef HAVE_GETOPT_H
		c = getopt_long (argc, argv, optchars, long_options, &option_index);
#else
		c = getopt (argc, argv, optchars);
#endif
		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?': /* usage */
			usage2 ("unknown argument", optarg);
			break;
		case 'h': /* help */
			print_help ();
			exit (STATE_OK);
			break;
		case 'V': /* version */
			print_revision (PROGNAME, REVISION);
			exit (STATE_OK);
			break;
		case 't': /* timeout period */
			if (!is_intnonneg (optarg))
				usage2 ("timeout interval must be a non-negative integer", optarg);
			socket_timeout = atoi (optarg);
			break;
		case 'c': /* critical time threshold */
			if (!is_intnonneg (optarg))
				usage2 ("invalid critical threshold", optarg);
			critical_time = atoi (optarg);
			check_critical_time = TRUE;
			break;
		case 'w': /* warning time threshold */
			if (!is_intnonneg (optarg))
				usage2 ("invalid warning threshold", optarg);
			warning_time = atoi (optarg);
			check_warning_time = TRUE;
			break;
		case 'L': /* show html link */
			display_html = TRUE;
			break;
		case 'n': /* do not show html link */
			display_html = FALSE;
			break;
		case 'S': /* use SSL */
#ifndef HAVE_SSL
			usage ("check_http: invalid option - SSL is not available\n");
#endif
			use_ssl = TRUE;
			if (specify_port == FALSE)
				server_port = HTTPS_PORT;
			break;
		case 'C': /* warning time threshold */
#ifdef HAVE_SSL
			if (!is_intnonneg (optarg))
				usage2 ("invalid certificate expiration period", optarg);
			days_till_exp = atoi (optarg);
			check_cert = TRUE;
#else
			usage ("check_http: invalid option - SSL is not available\n");
#endif
			break;
		case 'f': /* onredirect */
			if (!strcmp (optarg, "follow"))
				onredirect = STATE_DEPENDENT;
			if (!strcmp (optarg, "unknown"))
				onredirect = STATE_UNKNOWN;
			if (!strcmp (optarg, "ok"))
				onredirect = STATE_OK;
			if (!strcmp (optarg, "warning"))
				onredirect = STATE_WARNING;
			if (!strcmp (optarg, "critical"))
				onredirect = STATE_CRITICAL;
			if (verbose)
				printf("option f:%d \n", onredirect);  
			break;
		/* Note: H, I, and u must be malloc'd or will fail on redirects */
		case 'H': /* Host Name (virtual host) */
			host_name = strscpy (host_name, optarg);
			break;
		case 'I': /* Server IP-address */
			server_address = strscpy (server_address, optarg);
			break;
		case 'u': /* Host or server */
			server_url = strscpy (server_url, optarg);
			server_url_length = strlen (optarg);
			break;
		case 'p': /* Host or server */
			if (!is_intnonneg (optarg))
				usage2 ("invalid port number", optarg);
			server_port = atoi (optarg);
			specify_port = TRUE;
			break;
		case 'a': /* authorization info */
			strncpy (user_auth, optarg, MAX_INPUT_BUFFER - 1);
			user_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'P': /* HTTP POST data in URL encoded format */
			http_method = strscpy (http_method, "POST");
			http_post_data = strscpy (http_post_data, optarg);
			break;
		case 's': /* string or substring */
			strncpy (string_expect, optarg, MAX_INPUT_BUFFER - 1);
			string_expect[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'e': /* string or substring */
			strncpy (server_expect, optarg, MAX_INPUT_BUFFER - 1);
			server_expect[MAX_INPUT_BUFFER - 1] = 0;
			server_expect_yn = 1;
			break;
		case 'R': /* regex */
#ifdef HAVE_REGEX_H
			cflags = REG_ICASE;
#else
			usage ("check_http: call for regex which was not a compiled option\n");
#endif
		case 'r': /* regex */
#ifdef HAVE_REGEX_H
			cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
			strncpy (regexp, optarg, MAX_RE_SIZE - 1);
			regexp[MAX_RE_SIZE - 1] = 0;
			errcode = regcomp (&preg, regexp, cflags);
			if (errcode != 0) {
				(void) regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("Could Not Compile Regular Expression: %s", errbuf);
				return ERROR;
			}
#else
			usage ("check_http: call for regex which was not a compiled option\n");
#endif
			break;
		case 'v': /* verbose */
			verbose = TRUE;
			break;
		}
	}

	c = optind;

	if (server_address == NULL && host_name == NULL) {
		server_address = strscpy (NULL, argv[c]);
		host_name = strscpy (NULL, argv[c++]);
	}

	if (server_address == NULL && host_name == NULL)
		usage ("check_http: you must specify a host name\n");

	if (server_address == NULL)
		server_address = strscpy (NULL, host_name);

	if (host_name == NULL)
		host_name = strscpy (NULL, server_address);

	if (http_method == NULL)
		http_method = strscpy (http_method, "GET");

	if (server_url == NULL) {
		server_url = strscpy (NULL, "/");
		server_url_length = strlen(HTTP_URL);
	}

	return TRUE;
}



/* written by lauri alanko */
static char *
base64 (char *bin, int len)
{

	char *buf = (char *) malloc ((len + 2) / 3 * 4 + 1);
	int i = 0, j = 0;

	char BASE64_END = '=';
	char base64_table[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	while (j < len - 2) {
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[((bin[j] & 3) << 4) | (bin[j + 1] >> 4)];
		buf[i++] = base64_table[((bin[j + 1] & 15) << 2) | (bin[j + 2] >> 6)];
		buf[i++] = base64_table[bin[j + 2] & 63];
		j += 3;
	}

	switch (len - j) {
	case 1:
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[(bin[j] & 3) << 4];
		buf[i++] = BASE64_END;
		buf[i++] = BASE64_END;
		break;
	case 2:
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[((bin[j] & 3) << 4) | (bin[j + 1] >> 4)];
		buf[i++] = base64_table[(bin[j + 1] & 15) << 2];
		buf[i++] = BASE64_END;
		break;
	case 0:
		break;
	}

	buf[i] = '\0';
	return buf;
}



int
check_http (void)
{
	char *msg = NULL;
	char *status_line = NULL;
	char *header = NULL;
	char *page = NULL;
	char *auth = NULL;
	int i = 0;
	size_t pagesize = 0;
	char *full_page = NULL;
	char *buf = NULL;
	char *pos = NULL;
	char *x = NULL;
	char *orig_url = NULL;

	/* try to connect to the host at the given port number */
#ifdef HAVE_SSL
	if (use_ssl == TRUE) {

		if (connect_SSL () != OK)
			terminate (STATE_CRITICAL, "Unable to open TCP socket");

		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			X509_free (server_cert);
		}
		else {
			printf ("ERROR: Cannot retrieve server certificate.\n");
			return STATE_CRITICAL;
		}

		asprintf (&buf, "%s %s HTTP/1.0\r\n", http_method, server_url);
		if (SSL_write (ssl, buf, strlen (buf)) == -1) {
			ERR_print_errors_fp (stderr);
			return STATE_CRITICAL;
		}

		/* optionally send the host header info (not clear if it's usable) */
		if (strcmp (host_name, "")) {
			asprintf (&buf, "Host: %s\r\n", host_name);
			if (SSL_write (ssl, buf, strlen (buf)) == -1) {
				ERR_print_errors_fp (stderr);
				return STATE_CRITICAL;
			}
		}

		/* send user agent */
		asprintf (&buf, "User-Agent: check_http/%s (nagios-plugins %s)\r\n",
		         clean_revstring (REVISION), PACKAGE_VERSION);
		if (SSL_write (ssl, buf, strlen (buf)) == -1) {
			ERR_print_errors_fp (stderr);
			return STATE_CRITICAL;
		}

		/* optionally send the authentication info */
		if (strcmp (user_auth, "")) {
			auth = base64 (user_auth, strlen (user_auth));
			asprintf (&buf, "Authorization: Basic %s\r\n", auth);
			if (SSL_write (ssl, buf, strlen (buf)) == -1) {
				ERR_print_errors_fp (stderr);
				return STATE_CRITICAL;
			}
		}

		/* optionally send http POST data */
		if (http_post_data) {
			asprintf (&buf, "Content-Type: application/x-www-form-urlencoded\r\n");
			if (SSL_write (ssl, buf, strlen (buf)) == -1) {
				ERR_print_errors_fp (stderr);
				return STATE_CRITICAL;
			}
			asprintf (&buf, "Content-Length: %i\r\n\r\n", strlen (http_post_data));
			if (SSL_write (ssl, buf, strlen (buf)) == -1) {
				ERR_print_errors_fp (stderr);
				return STATE_CRITICAL;
			}
			http_post_data = strscat (http_post_data, "\r\n");
			if (SSL_write (ssl, http_post_data, strlen (http_post_data)) == -1) {
				ERR_print_errors_fp (stderr);
				return STATE_CRITICAL;
			}
		}

		/* send a newline so the server knows we're done with the request */
		asprintf (&buf, "\r\n\r\n");
		if (SSL_write (ssl, buf, strlen (buf)) == -1) {
			ERR_print_errors_fp (stderr);
			return STATE_CRITICAL;
		}

	}
	else {
#endif
		if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
			terminate (STATE_CRITICAL, "Unable to open TCP socket");
		asprintf (&buf, "%s %s HTTP/1.0\r\n", http_method, server_url);
		send (sd, buf, strlen (buf), 0);
		


		/* optionally send the host header info */
		if (strcmp (host_name, "")) {
			asprintf (&buf, "Host: %s\r\n", host_name);
			send (sd, buf, strlen (buf), 0);
		}

		/* send user agent */
		asprintf (&buf,
		         "User-Agent: check_http/%s (nagios-plugins %s)\r\n",
		         clean_revstring (REVISION), PACKAGE_VERSION);
		send (sd, buf, strlen (buf), 0);

		/* optionally send the authentication info */
		if (strcmp (user_auth, "")) {
			auth = base64 (user_auth, strlen (user_auth));
			asprintf (&buf, "Authorization: Basic %s\r\n", auth);
			send (sd, buf, strlen (buf), 0);
		}

		/* optionally send http POST data */
		/* written by Chris Henesy <lurker@shadowtech.org> */
		if (http_post_data) {
			asprintf (&buf, "Content-Type: application/x-www-form-urlencoded\r\n");
			send (sd, buf, strlen (buf), 0);
			asprintf (&buf, "Content-Length: %i\r\n\r\n", strlen (http_post_data));
			send (sd, buf, strlen (buf), 0);
			http_post_data = strscat (http_post_data, "\r\n");
			send (sd, http_post_data, strlen (http_post_data), 0);
		}

		/* send a newline so the server knows we're done with the request */
		asprintf (&buf, "\r\n\r\n");
		send (sd, buf, strlen (buf), 0);
#ifdef HAVE_SSL
	}
#endif

	/* fetch the page */
	pagesize = (size_t) 0;
	while ((i = my_recv ()) > 0) {
		buffer[i] = '\0'; 
		full_page = strscat (full_page, buffer);
		pagesize += i;
	}

	if (i < 0)
		terminate (STATE_CRITICAL, "Error in recv()");

	/* return a CRITICAL status if we couldn't read any data */
	if (pagesize == (size_t) 0)
		terminate (STATE_CRITICAL, "No data received %s", timestamp);

	/* close the connection */
	my_close ();

	/* reset the alarm */
	alarm (0);

	/* leave full_page untouched so we can free it later */
	page = full_page;

	if (verbose)
		printf ("Page is %d characters\n", pagesize);

	/* find status line and null-terminate it */
	status_line = page;
	page += (size_t) strcspn (page, "\r\n");
	pos = page;
	page += (size_t) strspn (page, "\r\n");
	status_line[pos - status_line] = 0;
	strip (status_line);
	if (verbose)
		printf ("STATUS: %s\n", status_line);

	/* find header info and null terminate it */
	header = page;
	while (strcspn (page, "\r\n") > 0) {
		page += (size_t) strcspn (page, "\r\n");
		pos = page;
		if ((strspn (page, "\r") == 1 && strspn (page, "\r\n") >= 2) ||
		    (strspn (page, "\n") == 1 && strspn (page, "\r\n") >= 2))
			page += (size_t) 2;
		else
			page += (size_t) 1;
	}
	page += (size_t) strspn (page, "\r\n");
	header[pos - header] = 0;
	if (verbose)
		printf ("**** HEADER ****\n%s\n**** CONTENT ****\n%s\n", header, page);

	/* make sure the status line matches the response we are looking for */
	if (!strstr (status_line, server_expect)) {
		if (server_port == HTTP_PORT)
			asprintf (&msg, "Invalid HTTP response received from host\n");
		else
			asprintf (&msg,
			                "Invalid HTTP response received from host on port %d\n",
			                server_port);
		terminate (STATE_CRITICAL, msg);
	}


	/* Exit here if server_expect was set by user and not default */
	if ( server_expect_yn  )  {
		asprintf (&msg, "HTTP OK: Status line output matched \"%s\"\n",
	                  server_expect);
		if (verbose)
			printf ("%s\n",msg);

	}
	else {
	

		/* check the return code */
		/* server errors result in a critical state */
		if (strstr (status_line, "500") ||
	  	  strstr (status_line, "501") ||
	    	strstr (status_line, "502") ||
		    strstr (status_line, "503")) {
			terminate (STATE_CRITICAL, "HTTP CRITICAL: %s\n", status_line);
		}

		/* client errors result in a warning state */
		if (strstr (status_line, "400") ||
	  	  strstr (status_line, "401") ||
	    	strstr (status_line, "402") ||
		    strstr (status_line, "403") ||
		    strstr (status_line, "404")) {
			terminate (STATE_WARNING, "HTTP WARNING: %s\n", status_line);
		}

		/* check redirected page if specified */
		if (strstr (status_line, "300") ||
	  	  strstr (status_line, "301") ||
	    	strstr (status_line, "302") ||
		    strstr (status_line, "303") ||
		    strstr (status_line, "304")) {
			if (onredirect == STATE_DEPENDENT) {

				orig_url = strscpy(NULL, server_url);
				pos = header;
				while (pos) {
					server_address = realloc (server_address, MAX_IPV4_HOSTLENGTH);
					if (server_address == NULL)
						terminate (STATE_UNKNOWN,
										 "HTTP UNKNOWN: could not allocate server_address");
					if (strcspn (pos, "\r\n") > server_url_length) {
						server_url = realloc (server_url, strcspn (pos, "\r\n"));
						if (server_url == NULL)
							terminate (STATE_UNKNOWN,
							           "HTTP UNKNOWN: could not allocate server_url");
						server_url_length = strcspn (pos, "\r\n");
					}
					if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST URI_PORT URI_PATH, server_type, server_address, server_port_text, server_url) == 4) {
						host_name = strscpy (host_name, server_address);
						use_ssl = server_type_check (server_type);
						server_port = atoi (server_port_text);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST URI_PATH, server_type, server_address, server_url) == 3 ) { 
						host_name = strscpy (host_name, server_address);
						use_ssl = server_type_check (server_type);
						server_port = server_port_check (use_ssl);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST URI_PORT, server_type, server_address, server_port_text) == 3) {
						host_name = strscpy (host_name, server_address);
						strcpy (server_url, "/");
						use_ssl = server_type_check (server_type);
						server_port = atoi (server_port_text);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST, server_type, server_address) == 2) {
						host_name = strscpy (host_name, server_address);
						strcpy (server_url, "/");
						use_ssl = server_type_check (server_type);
						server_port = server_port_check (use_ssl);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_PATH, server_url) == 1) {
						if ((server_url[0] != '/') && (x = strrchr(orig_url, '/'))) {
							*x = '\0';
							asprintf (&server_url, "%s/%s", orig_url, server_url);
						}
						check_http ();
					} 					
					pos += (size_t) strcspn (pos, "\r\n");
					pos += (size_t) strspn (pos, "\r\n");
				} /* end while (pos) */
				printf ("HTTP UNKNOWN: Could not find redirect location - %s%s",
				        status_line, (display_html ? "</A>" : ""));
				exit (STATE_UNKNOWN);
			} /* end if (onredirect == STATE_DEPENDENT) */
			
			else if (onredirect == STATE_UNKNOWN)
				printf ("HTTP UNKNOWN");
			else if (onredirect == STATE_OK)
				printf ("HTTP ok");
			else if (onredirect == STATE_WARNING)
				printf ("HTTP WARNING");
			else if (onredirect == STATE_CRITICAL)
				printf ("HTTP CRITICAL");
			time (&end_time);
			asprintf (&msg, ": %s - %d second response time %s%s|time=%d\n",
		                 status_line, (int) (end_time - start_time), timestamp,
	                   (display_html ? "</A>" : ""), (int) (end_time - start_time));
			terminate (onredirect, msg);
		} /* end if (strstr (status_line, "30[0-4]") */


	} /* end else (server_expect_yn)  */

		
	/* check elapsed time */
	time (&end_time);
	asprintf (&msg, "HTTP problem: %s - %d second response time %s%s|time=%d\n",
	               status_line, (int) (end_time - start_time), timestamp,
	               (display_html ? "</A>" : ""), (int) (end_time - start_time));
	if (check_critical_time == TRUE && (end_time - start_time) > critical_time)
		terminate (STATE_CRITICAL, msg);
	if (check_warning_time == TRUE && (end_time - start_time) > warning_time)
		terminate (STATE_WARNING, msg);

	/* Page and Header content checks go here */
	/* these checks should be last */

	if (strlen (string_expect)) {
		if (strstr (page, string_expect)) {
			printf ("HTTP ok: %s - %d second response time %s%s|time=%d\n",
			        status_line, (int) (end_time - start_time),
			        timestamp, (display_html ? "</A>" : ""), (int) (end_time - start_time));
			exit (STATE_OK);
		}
		else {
			printf ("HTTP CRITICAL: string not found%s|time=%d\n",
			        (display_html ? "</A>" : ""), (int) (end_time - start_time));
			exit (STATE_CRITICAL);
		}
	}
#ifdef HAVE_REGEX_H
	if (strlen (regexp)) {
		errcode = regexec (&preg, page, REGS, pmatch, 0);
		if (errcode == 0) {
			printf ("HTTP ok: %s - %d second response time %s%s|time=%d\n",
			        status_line, (int) (end_time - start_time),
			        timestamp, (display_html ? "</A>" : ""), (int) (end_time - start_time));
			exit (STATE_OK);
		}
		else {
			if (errcode == REG_NOMATCH) {
				printf ("HTTP CRITICAL: pattern not found%s|time=%d\n",
				        (display_html ? "</A>" : ""), (int) (end_time - start_time));
				exit (STATE_CRITICAL);
			}
			else {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("Execute Error: %s\n", errbuf);
				exit (STATE_CRITICAL);
			}
		}
	}
#endif

	/* We only get here if all tests have been passed */
	asprintf (&msg, "HTTP ok: %s - %d second response time %s%s|time=%d\n",
	                status_line, (int) (end_time - start_time),
	                timestamp, (display_html ? "</A>" : ""), (int) (end_time - start_time));
	terminate (STATE_OK, msg);
	return STATE_UNKNOWN;
}



#ifdef HAVE_SSL
int connect_SSL (void)
{
	SSL_METHOD *meth;

	randbuff = strscpy (NULL, "qwertyuiopasdfghjkl");
	RAND_seed (randbuff, strlen (randbuff));
	/* Initialize SSL context */
	SSLeay_add_ssl_algorithms ();
	meth = SSLv23_client_method ();
	SSL_load_error_strings ();
	if ((ctx = SSL_CTX_new (meth)) == NULL) {
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
	if (my_tcp_connect (server_address, server_port, &sd) == STATE_OK) {
		/* Do the SSL handshake */
		if ((ssl = SSL_new (ctx)) != NULL) {
			SSL_set_cipher_list(ssl, "ALL");
			SSL_set_fd (ssl, sd);
			if (SSL_connect (ssl) != -1)
				return OK;
			ERR_print_errors_fp (stderr);
		}
		else {
			printf ("ERROR: Cannot initiate SSL handshake.\n");
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
			printf ("ERROR: Wrong time format in certificate.\n");
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
			printf ("ERROR: Wrong time format in certificate.\n");
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
		i = SSL_read (ssl, buffer, MAX_INPUT_BUFFER - 1);
	}
	else {
		i = recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0);
	}
#else
	i = recv (sd, buffer, MAX_INPUT_BUFFER - 1, 0);
#endif
	return i;
}


int
my_close (void)
{
#ifdef HAVE_SSL
	if (use_ssl == TRUE) {
		SSL_shutdown (ssl);
		SSL_free (ssl);
		SSL_CTX_free (ctx);
		return 0;
	}
	else {
#endif
		return close (sd);
#ifdef HAVE_SSL
	}
#endif
}



void
print_help (void)
{
	print_revision (PROGNAME, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHORS, EMAIL, SUMMARY);
	print_usage ();
	printf ("NOTE: One or both of -H and -I must be specified\n");
	printf ("\nOptions:\n" LONGOPTIONS "\n", HTTP_EXPECT, HTTP_PORT,
	        DEFAULT_SOCKET_TIMEOUT, SSLOPTIONS);
#ifdef HAVE_SSL
	printf (SSLDESCRIPTION);
#endif
}


void
print_usage (void)
{
	printf ("Usage:\n" " %s %s\n"
#ifdef HAVE_GETOPT_H
	        " %s (-h | --help) for detailed help\n"
	        " %s (-V | --version) for version information\n",
#else
	        " %s -h for detailed help\n"
	        " %s -V for version information\n",
#endif
	PROGNAME, OPTIONS, PROGNAME, PROGNAME);
}
