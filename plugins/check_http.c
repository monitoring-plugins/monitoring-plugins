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

const char *progname = "check_http";
#define REVISION "$Revision$"
#define COPYRIGHT "1999-2001"
#define AUTHORS "Ethan Galstad/Karl DeBisschop"
#define EMAIL "kdebisschop@users.sourceforge.net"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"

#define SUMMARY "\
This plugin tests the HTTP service on the specified host. It can test\n\
normal (http) and secure (https) servers, follow redirects, search for\n\
strings and regular expressions, check connection times, and report on\n\
certificate expiration times.\n"

#define OPTIONS "\
(-H <vhost> | -I <IP-address>) [-u <uri>] [-p <port>]\n\
            [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L]\n\
            [-a auth] [-f <ok | warn | critcal | follow>] [-e <expect>]\n\
            [-s string] [-l] [-r <regex> | -R <case-insensitive regex>]\n\
            [-P string] [-m min_pg_size]"

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
   How to handle redirected pages\n%s%s\
-m, --min=INTEGER\n\
   Minimum page size required (bytes)\n\
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

#ifdef HAVE_REGEX_H
#define REGOPTIONS "\
 -l, --linespan\n\
    Allow regex to span newlines (must precede -r or -R)\n\
 -r, --regex, --ereg=STRING\n\
    Search page for regex STRING\n\
 -R, --eregi=STRING\n\
    Search page for case-insensitive regex STRING\n"
#else
#define REGOPTIONS ""
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
char *randbuff = "";
SSL_CTX *ctx;
SSL *ssl;
X509 *server_cert;
int connect_SSL (void);
int check_certificate (X509 **);
#endif

#ifdef HAVE_REGEX_H
enum {
	REGS = 2,
	MAX_RE_SIZE = 256
};
#include <regex.h>
regex_t preg;
regmatch_t pmatch[REGS];
char regexp[MAX_RE_SIZE];
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
int errcode;
#endif

struct timeval tv;

#define server_type_check(server_type) \
(strcmp (server_type, "https") ? FALSE : TRUE)

#define server_port_check(use_ssl) (use_ssl ? HTTPS_PORT : HTTP_PORT)

#define HDR_LOCATION "%*[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]: "
#define URI_HTTP "%[HTPShtps]://"
#define URI_HOST "%[-.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define URI_PORT ":%[-0123456789]"
#define URI_PATH "%[-_=@,?&#;/.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"

enum {
	MAX_IPV4_HOSTLENGTH = 255,
	HTTP_PORT = 80,
	HTTPS_PORT = 443
};

#define HTTP_EXPECT "HTTP/1."
#define HTTP_URL "/"
#define CRLF "\r\n"

char timestamp[17] = "";
int specify_port = FALSE;
int server_port = HTTP_PORT;
char server_port_text[6] = "";
char server_type[6] = "http";
char *server_address = ""; 
char *host_name = "";
char *server_url = "";
int server_url_length;
int server_expect_yn = 0;
char server_expect[MAX_INPUT_BUFFER] = HTTP_EXPECT;
char string_expect[MAX_INPUT_BUFFER] = "";
double warning_time = 0;
int check_warning_time = FALSE;
double critical_time = 0;
int check_critical_time = FALSE;
char user_auth[MAX_INPUT_BUFFER] = "";
int display_html = FALSE;
int onredirect = STATE_OK;
int use_ssl = FALSE;
int verbose = FALSE;
int sd;
int min_page_len = 0;
char *http_method = "GET";
char *http_post_data = "";
char buffer[MAX_INPUT_BUFFER];

void print_usage (void);
void print_help (void);
int process_arguments (int, char **);
static char *base64 (char *bin, int len);
int check_http (void);
int my_recv (void);
int my_close (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	/* Set default URL. Must be malloced for subsequent realloc if --onredirect=follow */
	asprintf (&server_url, "%s", HTTP_URL);
	server_url_length = strlen(server_url);

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
	gettimeofday (&tv, NULL);

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
	int c = 1;

	int option_index = 0;
	static struct option long_options[] = {
		STD_LONG_OPTS,
		{"file",required_argument,0,'F'},
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
 		{"linespan", no_argument, 0, 'l'},
		{"onredirect", required_argument, 0, 'f'},
		{"certificate", required_argument, 0, 'C'},
		{"min", required_argument, 0, 'm'},
		{0, 0, 0, 0}
	};

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

	while (1) {
		c = getopt_long (argc, argv, "Vvht:c:w:H:P:I:a:e:p:s:R:r:u:f:C:nlLSm:", long_options, &option_index);
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
			print_revision (progname, REVISION);
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
			critical_time = strtod (optarg, NULL);
			check_critical_time = TRUE;
			break;
		case 'w': /* warning time threshold */
			if (!is_intnonneg (optarg))
				usage2 ("invalid warning threshold", optarg);
			warning_time = strtod (optarg, NULL);
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
		case 'C': /* Check SSL cert validity */
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
 			asprintf (&host_name, "%s", optarg);
			break;
		case 'I': /* Server IP-address */
 			asprintf (&server_address, "%s", optarg);
			break;
		case 'u': /* Host or server */
			asprintf (&server_url, "%s", optarg);
			server_url_length = strlen (server_url);
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
			asprintf (&http_method, "%s", "POST");
			asprintf (&http_post_data, "%s", optarg);
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
#ifndef HAVE_REGEX_H
 		case 'l': /* linespan */
 		case 'r': /* linespan */
 		case 'R': /* linespan */
			usage ("check_http: call for regex which was not a compiled option\n");
			break;
#else
 		case 'l': /* linespan */
 			cflags &= ~REG_NEWLINE;
 			break;
		case 'R': /* regex */
			cflags |= REG_ICASE;
		case 'r': /* regex */
			strncpy (regexp, optarg, MAX_RE_SIZE - 1);
			regexp[MAX_RE_SIZE - 1] = 0;
			errcode = regcomp (&preg, regexp, cflags);
			if (errcode != 0) {
				(void) regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("Could Not Compile Regular Expression: %s", errbuf);
				return ERROR;
			}
			break;
#endif
		case 'v': /* verbose */
			verbose = TRUE;
			break;
		case 'm': /* min_page_length */
			min_page_len = atoi (optarg);
			break;
		}
	}

	c = optind;

	if (strcmp (server_address, "") == 0 && c < argc)
			asprintf (&server_address, "%s", argv[c++]);

	if (strcmp (host_name, "") == 0 && c < argc)
 		asprintf (&host_name, "%s", argv[c++]);

	if (strcmp (server_address ,"") == 0) {
		if (strcmp (host_name, "") == 0)
			usage ("check_http: you must specify a server address or host name\n");
		else
			asprintf (&server_address, "%s", host_name);
	}

	if (check_critical_time && critical_time>(double)socket_timeout)
		socket_timeout = (int)critical_time + 1;

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
	char *status_line = "";
	char *header = NULL;
	char *page = "";
	char *auth = NULL;
	int i = 0;
	size_t pagesize = 0;
	char *full_page = "";
	char *buf = NULL;
	char *pos = "";
	char *x = NULL;
	char *orig_url = NULL;
	double elapsed_time;
	int page_len = 0;
#ifdef HAVE_SSL
	int sslerr;
#endif

	/* try to connect to the host at the given port number */
#ifdef HAVE_SSL
	if (use_ssl == TRUE) {

		if (connect_SSL () != OK) {
			terminate (STATE_CRITICAL, "Unable to open TCP socket");
		}

		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			X509_free (server_cert);
		}
		else {
			printf ("ERROR: Cannot retrieve server certificate.\n");
			return STATE_CRITICAL;
		}

	}
	else {
#endif
		if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
			terminate (STATE_CRITICAL, "Unable to open TCP socket");
#ifdef HAVE_SSL
	}
#endif

	asprintf (&buf, "%s %s HTTP/1.0\r\n", http_method, server_url);

	/* optionally send the host header info (not clear if it's usable) */
	if (strcmp (host_name, ""))
		asprintf (&buf, "%sHost: %s\r\n", buf, host_name);

	/* send user agent */
	asprintf (&buf, "%sUser-Agent: check_http/%s (nagios-plugins %s)\r\n",
	          buf, clean_revstring (REVISION), PACKAGE_VERSION);

	/* optionally send the authentication info */
	if (strcmp (user_auth, "")) {
		auth = base64 (user_auth, strlen (user_auth));
		asprintf (&buf, "%sAuthorization: Basic %s\r\n", buf, auth);
	}

	/* either send http POST data */
	if (strlen (http_post_data)) {
		asprintf (&buf, "%sContent-Type: application/x-www-form-urlencoded\r\n", buf);
		asprintf (&buf, "%sContent-Length: %i\r\n\r\n", buf, strlen (http_post_data));
		asprintf (&buf, "%s%s%s", buf, http_post_data, CRLF);
	}
	else {
		/* or just a newline so the server knows we're done with the request */
		asprintf (&buf, "%s%s", buf, CRLF);
	}

#ifdef HAVE_SSL
	if (use_ssl == TRUE) {
		if (SSL_write (ssl, buf, strlen (buf)) == -1) {
			ERR_print_errors_fp (stderr);
			return STATE_CRITICAL;
		}
	}
	else {
#endif
		send (sd, buf, strlen (buf), 0);
#ifdef HAVE_SSL
	}
#endif

	/* fetch the page */
	while ((i = my_recv ()) > 0) {
		buffer[i] = '\0';
		asprintf (&full_page, "%s%s", full_page, buffer);
		pagesize += i;
	}

	if (i < 0 && errno != ECONNRESET) {
#ifdef HAVE_SSL
		if (use_ssl) {
			sslerr=SSL_get_error(ssl, i);
			if ( sslerr == SSL_ERROR_SSL ) {
				terminate (STATE_WARNING, "Client Certificate Required\n");
			} else {
				terminate (STATE_CRITICAL, "Error in recv()");
			}
		}
		else {
#endif
			terminate (STATE_CRITICAL, "Error in recv()");
#ifdef HAVE_SSL
		}
#endif
	}

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
	status_line[strcspn(status_line, "\r\n")] = 0;
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

				asprintf (&orig_url, "%s", server_url);
				pos = header;
				while (pos) {
					server_address = realloc (server_address, MAX_IPV4_HOSTLENGTH + 1);
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
						asprintf (&host_name, "%s", server_address);
						use_ssl = server_type_check (server_type);
						server_port = atoi (server_port_text);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST URI_PATH, server_type, server_address, server_url) == 3 ) { 
						asprintf (&host_name, "%s", server_address);
						use_ssl = server_type_check (server_type);
						server_port = server_port_check (use_ssl);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST URI_PORT, server_type, server_address, server_port_text) == 3) {
						asprintf (&host_name, "%s", server_address);
						strcpy (server_url, "/");
						use_ssl = server_type_check (server_type);
						server_port = atoi (server_port_text);
						check_http ();
					}
					else if (sscanf (pos, HDR_LOCATION URI_HTTP URI_HOST, server_type, server_address) == 2) {
						asprintf (&host_name, "%s", server_address);
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
				printf ("UNKNOWN - Could not find redirect location - %s%s",
				        status_line, (display_html ? "</A>" : ""));
				exit (STATE_UNKNOWN);
			} /* end if (onredirect == STATE_DEPENDENT) */
			
			else if (onredirect == STATE_UNKNOWN)
				printf ("UNKNOWN");
			else if (onredirect == STATE_OK)
				printf ("OK");
			else if (onredirect == STATE_WARNING)
				printf ("WARNING");
			else if (onredirect == STATE_CRITICAL)
				printf ("CRITICAL");
			elapsed_time = delta_time (tv);
			asprintf (&msg, " - %s - %.3f second response time %s%s|time=%.3f\n",
		                 status_line, elapsed_time, timestamp,
	                   (display_html ? "</A>" : ""), elapsed_time);
			terminate (onredirect, msg);
		} /* end if (strstr (status_line, "30[0-4]") */


	} /* end else (server_expect_yn)  */

		
	/* check elapsed time */
	elapsed_time = delta_time (tv);
	asprintf (&msg, "HTTP problem: %s - %.3f second response time %s%s|time=%.3f\n",
	               status_line, elapsed_time, timestamp,
	               (display_html ? "</A>" : ""), elapsed_time);
	if (check_critical_time == TRUE && elapsed_time > critical_time)
		terminate (STATE_CRITICAL, msg);
	if (check_warning_time == TRUE && elapsed_time > warning_time)
		terminate (STATE_WARNING, msg);

	/* Page and Header content checks go here */
	/* these checks should be last */

	if (strlen (string_expect)) {
		if (strstr (page, string_expect)) {
			printf ("HTTP OK %s - %.3f second response time %s%s|time=%.3f\n",
			        status_line, elapsed_time,
			        timestamp, (display_html ? "</A>" : ""), elapsed_time);
			exit (STATE_OK);
		}
		else {
			printf ("CRITICAL - string not found%s|time=%.3f\n",
			        (display_html ? "</A>" : ""), elapsed_time);
			exit (STATE_CRITICAL);
		}
	}
#ifdef HAVE_REGEX_H
	if (strlen (regexp)) {
		errcode = regexec (&preg, page, REGS, pmatch, 0);
		if (errcode == 0) {
			printf ("HTTP OK %s - %.3f second response time %s%s|time=%.3f\n",
			        status_line, elapsed_time,
			        timestamp, (display_html ? "</A>" : ""), elapsed_time);
			exit (STATE_OK);
		}
		else {
			if (errcode == REG_NOMATCH) {
				printf ("CRITICAL - pattern not found%s|time=%.3f\n",
				        (display_html ? "</A>" : ""), elapsed_time);
				exit (STATE_CRITICAL);
			}
			else {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf ("CRITICAL - Execute Error: %s\n", errbuf);
				exit (STATE_CRITICAL);
			}
		}
	}
#endif

	/* make sure the page is of an appropriate size */
	page_len = strlen (page);
	if ((min_page_len > 0) && (page_len < min_page_len)) {
		printf ("HTTP WARNING: page size too small%s|size=%i\n",
			(display_html ? "</A>" : ""), page_len );
		exit (STATE_WARNING);
	}
	/* We only get here if all tests have been passed */
	asprintf (&msg, "HTTP OK %s - %.3f second response time %s%s|time=%.3f\n",
	                status_line, (float)elapsed_time,
	                timestamp, (display_html ? "</A>" : ""), elapsed_time);
	terminate (STATE_OK, msg);
	return STATE_UNKNOWN;
}



#ifdef HAVE_SSL
int connect_SSL (void)
{
	SSL_METHOD *meth;

	asprintf (&randbuff, "%s", "qwertyuiopasdfghjklqwertyuiopasdfghjkl");
	RAND_seed (randbuff, strlen (randbuff));
	if (verbose)
		printf("SSL seeding: %s\n", (RAND_status()==1 ? "OK" : "Failed") );

	/* Initialize SSL context */
	SSLeay_add_ssl_algorithms ();
	meth = SSLv23_client_method ();
	SSL_load_error_strings ();
	if ((ctx = SSL_CTX_new (meth)) == NULL) {
		printf ("CRITICAL -  Cannot create SSL context.\n");
		return STATE_CRITICAL;
	}

	/* Initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* Set socket timeout */
	alarm (socket_timeout);

	/* Save start time */
	gettimeofday (&tv, NULL);

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
			printf ("CRITICAL - Cannot initiate SSL handshake.\n");
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
		printf ("WARNING - Certificate expires in %d day(s) (%s).\n", days_left, timestamp);
		return STATE_WARNING;
	}
	if (days_left < 0) {
		printf ("CRITICAL - Certificate expired on %s.\n", timestamp);
		return STATE_CRITICAL;
	}

	if (days_left == 0) {
		printf ("WARNING - Certificate expires today (%s).\n", timestamp);
		return STATE_WARNING;
	}

	printf ("OK - Certificate will expire on %s.\n", timestamp);

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
	print_revision (progname, REVISION);
	printf
		("Copyright (c) %s %s <%s>\n\n%s\n",
		 COPYRIGHT, AUTHORS, EMAIL, SUMMARY);
	print_usage ();
	printf ("NOTE: One or both of -H and -I must be specified\n");
	printf ("\nOptions:\n" LONGOPTIONS "\n", HTTP_EXPECT, HTTP_PORT,
	        DEFAULT_SOCKET_TIMEOUT, SSLOPTIONS, REGOPTIONS);
#ifdef HAVE_SSL
	printf (SSLDESCRIPTION);
#endif
}


void
print_usage (void)
{
	printf ("\
Usage:\n\
 %s %s\n\
 %s (-h | --help) for detailed help\n\
 %s (-V | --version) for version information\n",
	progname, OPTIONS, progname, progname);
}
