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

******************************************************************************/

const char *progname = "check_http";
const char *revision = "$Revision$";
const char *copyright = "1999-2001";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

#define HTTP_EXPECT "HTTP/1."
enum {
	MAX_IPV4_HOSTLENGTH = 255,
	HTTP_PORT = 80,
	HTTPS_PORT = 443
};

#ifdef HAVE_SSL_H
#include <rsa.h>
#include <crypto.h>
#include <x509.h>
#include <pem.h>
#include <ssl.h>
#include <err.h>
#include <rand.h>
#else
# ifdef HAVE_OPENSSL_SSL_H
# include <openssl/rsa.h>
# include <openssl/crypto.h>
# include <openssl/x509.h>
# include <openssl/pem.h>
# include <openssl/ssl.h>
# include <openssl/err.h>
# include <openssl/rand.h>
# endif
#endif

#ifdef HAVE_SSL
int check_cert = FALSE;
int days_till_exp;
char *randbuff;
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

#define HTTP_URL "/"
#define CRLF "\r\n"

char timestamp[17] = "";
int specify_port = FALSE;
int server_port = HTTP_PORT;
char server_port_text[6] = "";
char server_type[6] = "http";
char *server_address;
char *host_name;
char *server_url;
char *user_agent;
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
int redir_depth = 0;
int max_depth = 15;
char *http_method;
char *http_post_data;
char buffer[MAX_INPUT_BUFFER];

int process_arguments (int, char **);
static char *base64 (const char *bin, size_t len);
int check_http (void);
int redir (char *pos, char *status_line);
int server_type_check(const char *type);
int server_port_check(int ssl_flag);
int my_recv (void);
int my_close (void);
void print_help (void);
void print_usage (void);

int
main (int argc, char **argv)
{
	int result = STATE_UNKNOWN;

	/* Set default URL. Must be malloced for subsequent realloc if --onredirect=follow */
	server_url = strdup(HTTP_URL);
	server_url_length = strlen(server_url);
	asprintf (&user_agent, "User-Agent: check_http/%s (nagios-plugins %s)",
	          clean_revstring (revision), VERSION);

	if (process_arguments (argc, argv) == ERROR)
		usage (_("check_http: could not parse arguments\n"));

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
			die (STATE_CRITICAL, _("HTTP CRITICAL - Could not make SSL connection\n"));
		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			result = check_certificate (&server_cert);
			X509_free (server_cert);
		}
		else {
			printf (_("ERROR: Cannot retrieve server certificate.\n"));
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

	int option = 0;
	static struct option longopts[] = {
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
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
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
		c = getopt_long (argc, argv, "Vvh46t:c:w:H:P:I:a:e:p:s:R:r:u:f:C:nlLSm:", longopts, &option);
		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case '?': /* usage */
			usage3 (_("unknown argument"), optopt);
			break;
		case 'h': /* help */
			print_help ();
			exit (STATE_OK);
			break;
		case 'V': /* version */
			print_revision (progname, revision);
			exit (STATE_OK);
			break;
		case 't': /* timeout period */
			if (!is_intnonneg (optarg))
				usage2 (_("timeout interval must be a non-negative integer"), optarg);
			else
				socket_timeout = atoi (optarg);
			break;
		case 'c': /* critical time threshold */
			if (!is_intnonneg (optarg))
				usage2 (_("invalid critical threshold"), optarg);
			else {
				critical_time = strtod (optarg, NULL);
				check_critical_time = TRUE;
			}
			break;
		case 'w': /* warning time threshold */
			if (!is_intnonneg (optarg))
				usage2 (_("invalid warning threshold"), optarg);
			else {
				warning_time = strtod (optarg, NULL);
				check_warning_time = TRUE;
			}
			break;
		case 'L': /* show html link */
			display_html = TRUE;
			break;
		case 'n': /* do not show html link */
			display_html = FALSE;
			break;
		case 'S': /* use SSL */
#ifndef HAVE_SSL
			usage (_("check_http: invalid option - SSL is not available\n"));
#endif
			use_ssl = TRUE;
			if (specify_port == FALSE)
				server_port = HTTPS_PORT;
			break;
		case 'C': /* Check SSL cert validity */
#ifdef HAVE_SSL
			if (!is_intnonneg (optarg))
				usage2 (_("invalid certificate expiration period"), optarg);
			else {
				days_till_exp = atoi (optarg);
				check_cert = TRUE;
			}
#else
			usage (_("check_http: invalid option - SSL is not available\n"));
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
				printf(_("option f:%d \n"), onredirect);  
			break;
		/* Note: H, I, and u must be malloc'd or will fail on redirects */
		case 'H': /* Host Name (virtual host) */
 			host_name = strdup (optarg);
			break;
		case 'I': /* Server IP-address */
 			server_address = strdup (optarg);
			break;
		case 'u': /* URL path */
			server_url = strdup (optarg);
			server_url_length = strlen (server_url);
			break;
		case 'p': /* Server port */
			if (!is_intnonneg (optarg))
				usage2 (_("invalid port number"), optarg);
			else {
				server_port = atoi (optarg);
				specify_port = TRUE;
			}
			break;
		case 'a': /* authorization info */
			strncpy (user_auth, optarg, MAX_INPUT_BUFFER - 1);
			user_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'P': /* HTTP POST data in URL encoded format */
			if (http_method || http_post_data) break;
			http_method = strdup("POST");
			http_post_data = strdup (optarg);
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
			usage (_("check_http: call for regex which was not a compiled option\n"));
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
				printf (_("Could Not Compile Regular Expression: %s"), errbuf);
				return ERROR;
			}
			break;
#endif
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
		case 'v': /* verbose */
			verbose = TRUE;
			break;
		case 'm': /* min_page_length */
			min_page_len = atoi (optarg);
			break;
		}
	}

	c = optind;

	if (server_address == NULL && c < argc)
		server_address = strdup (argv[c++]);

	if (host_name == NULL && c < argc)
 		host_name = strdup (argv[c++]);

	if (server_address == NULL) {
		if (host_name == NULL)
			usage (_("check_http: you must specify a server address or host name\n"));
		else
			server_address = strdup (host_name);
	}

	if (check_critical_time && critical_time>(double)socket_timeout)
		socket_timeout = (int)critical_time + 1;

	if (http_method == NULL)
		http_method = strdup ("GET");

	return TRUE;
}



/* written by lauri alanko */
static char *
base64 (const char *bin, size_t len)
{

	char *buf = (char *) malloc ((len + 2) / 3 * 4 + 1);
	size_t i = 0, j = 0;

	char BASE64_END = '=';
	char base64_table[64];
	strncpy (base64_table, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", 64);

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
	char *msg;
	char *status_line;
	char *header;
	char *page;
	char *auth;
	int i = 0;
	size_t pagesize = 0;
	char *full_page;
	char *buf;
	char *pos;
	long microsec;
	double elapsed_time;
	int page_len = 0;
#ifdef HAVE_SSL
	int sslerr;
#endif

	/* try to connect to the host at the given port number */
#ifdef HAVE_SSL
	if (use_ssl == TRUE) {

		if (connect_SSL () != OK) {
			die (STATE_CRITICAL, _("Unable to open TCP socket\n"));
		}

		if ((server_cert = SSL_get_peer_certificate (ssl)) != NULL) {
			X509_free (server_cert);
		}
		else {
			printf (_("ERROR: Cannot retrieve server certificate.\n"));
			return STATE_CRITICAL;
		}

	}
	else {
#endif
		if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
			die (STATE_CRITICAL, _("Unable to open TCP socket\n"));
#ifdef HAVE_SSL
	}
#endif

	asprintf (&buf, "%s %s HTTP/1.0\r\n%s\r\n", http_method, server_url, user_agent);

	/* optionally send the host header info */
	if (host_name)
		asprintf (&buf, "%sHost: %s\r\n", buf, host_name);

	/* optionally send the authentication info */
	if (strlen(user_auth)) {
		auth = base64 (user_auth, strlen (user_auth));
		asprintf (&buf, "%sAuthorization: Basic %s\r\n", buf, auth);
	}

	/* either send http POST data */
	if (http_post_data) {
		asprintf (&buf, "%sContent-Type: application/x-www-form-urlencoded\r\n", buf);
		asprintf (&buf, "%sContent-Length: %i\r\n\r\n", buf, strlen (http_post_data));
		asprintf (&buf, "%s%s%s", buf, http_post_data, CRLF);
	}
	else {
		/* or just a newline so the server knows we're done with the request */
		asprintf (&buf, "%s%s", buf, CRLF);
	}

	if (verbose)
		printf ("%s\n", buf);

#ifdef HAVE_SSL
	if (use_ssl == TRUE) {
		if (SSL_write (ssl, buf, (int)strlen(buf)) == -1) {
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
	full_page = strdup("");
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
				die (STATE_WARNING, _("Client Certificate Required\n"));
			} else {
				die (STATE_CRITICAL, _("Error in recv()\n"));
			}
		}
		else {
#endif
			die (STATE_CRITICAL, _("Error in recv()\n"));
#ifdef HAVE_SSL
		}
#endif
	}

	/* return a CRITICAL status if we couldn't read any data */
	if (pagesize == (size_t) 0)
		die (STATE_CRITICAL, _("No data received %s\n"), timestamp);

	/* close the connection */
	my_close ();

	/* reset the alarm */
	alarm (0);

	/* leave full_page untouched so we can free it later */
	page = full_page;

	if (verbose)
		printf ("%s://%s:%d%s is %d characters\n", server_type, server_address, server_port, server_url, pagesize);

	/* find status line and null-terminate it */
	status_line = page;
	page += (size_t) strcspn (page, "\r\n");
	pos = page;
	page += (size_t) strspn (page, "\r\n");
	status_line[strcspn(status_line, "\r\n")] = 0;
	strip (status_line);
	if (verbose)
		printf ("STATUS: %s\n", status_line);

	/* find header info and null-terminate it */
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
			asprintf (&msg, _("Invalid HTTP response received from host\n"));
		else
			asprintf (&msg,
			                _("Invalid HTTP response received from host on port %d\n"),
			                server_port);
		die (STATE_CRITICAL, "%s", msg);
	}

	/* Exit here if server_expect was set by user and not default */
	if ( server_expect_yn  )  {
		asprintf (&msg, _("HTTP OK: Status line output matched \"%s\"\n"),
	                  server_expect);
		if (verbose)
			printf ("%s\n",msg);

	}
	else {
	

		/* check the return code */
		/* server errors result in a critical state */
		if (strstr (status_line, "500") || strstr (status_line, "501") ||
		    strstr (status_line, "502") || strstr (status_line, "503") ||
		    strstr (status_line, "504") || strstr (status_line, "505")) {
 			die (STATE_CRITICAL, _("HTTP CRITICAL: %s\n"), status_line);
		}

		/* client errors result in a warning state */
		if (strstr (status_line, "400") || strstr (status_line, "401") ||
		    strstr (status_line, "402") || strstr (status_line, "403") ||
		    strstr (status_line, "404") || strstr (status_line, "405") ||
		    strstr (status_line, "406") || strstr (status_line, "407") ||
		    strstr (status_line, "408") || strstr (status_line, "409") ||
		    strstr (status_line, "410") || strstr (status_line, "411") ||
		    strstr (status_line, "412") || strstr (status_line, "413") ||
		    strstr (status_line, "414") || strstr (status_line, "415") ||
		    strstr (status_line, "416") || strstr (status_line, "417")) {
			die (STATE_WARNING, _("HTTP WARNING: %s\n"), status_line);
		}

		/* check redirected page if specified */
		if (strstr (status_line, "300") || strstr (status_line, "301") ||
		    strstr (status_line, "302") || strstr (status_line, "303") ||
		    strstr (status_line, "304") || strstr (status_line, "305") ||
		    strstr (status_line, "306")) {

			if (onredirect == STATE_DEPENDENT)
				redir (header, status_line);
			else if (onredirect == STATE_UNKNOWN)
				printf (_("UNKNOWN"));
			else if (onredirect == STATE_OK)
				printf (_("OK"));
			else if (onredirect == STATE_WARNING)
				printf (_("WARNING"));
			else if (onredirect == STATE_CRITICAL)
				printf (_("CRITICAL"));
			microsec = deltime (tv);
			elapsed_time = (double)microsec / 1.0e6;
			asprintf (&msg, _(" - %s - %.3f second response time %s%s|time=%ldus size=%dB\n"),
		                 status_line, elapsed_time, timestamp,
	                   (display_html ? "</A>" : ""), microsec, pagesize);
			die (onredirect, "%s", msg);
		} /* end if (strstr (status_line, "30[0-4]") */


	} /* end else (server_expect_yn)  */

		
	/* check elapsed time */
	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;
	asprintf (&msg, _("HTTP problem: %s - %.3f second response time %s%s|time=%ldus size=%dB\n"),
	               status_line, elapsed_time, timestamp,
	               (display_html ? "</A>" : ""), microsec, pagesize);
	if (check_critical_time == TRUE && elapsed_time > critical_time)
		die (STATE_CRITICAL, "%s", msg);
	if (check_warning_time == TRUE && elapsed_time > warning_time)
		die (STATE_WARNING, "%s", msg);

	/* Page and Header content checks go here */
	/* these checks should be last */

	if (strlen (string_expect)) {
		if (strstr (page, string_expect)) {
			printf (_("HTTP OK %s - %.3f second response time %s%s|time=%ldus size=%dB\n"),
			        status_line, elapsed_time,
			        timestamp, (display_html ? "</A>" : ""), microsec, pagesize);
			exit (STATE_OK);
		}
		else {
			printf (_("CRITICAL - string not found%s|time=%ldus\n size=%dB"),
			        (display_html ? "</A>" : ""), microsec, pagesize);
			exit (STATE_CRITICAL);
		}
	}
#ifdef HAVE_REGEX_H
	if (strlen (regexp)) {
		errcode = regexec (&preg, page, REGS, pmatch, 0);
		if (errcode == 0) {
			printf (_("HTTP OK %s - %.3f second response time %s%s|time=%ldus size=%dB\n"),
			        status_line, elapsed_time,
			        timestamp, (display_html ? "</A>" : ""), microsec, pagesize);
			exit (STATE_OK);
		}
		else {
			if (errcode == REG_NOMATCH) {
				printf (_("CRITICAL - pattern not found%s|time=%ldus size=%dB\n"),
				        (display_html ? "</A>" : ""), microsec, pagesize);
				exit (STATE_CRITICAL);
			}
			else {
				regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf (_("CRITICAL - Execute Error: %s\n"), errbuf);
				exit (STATE_CRITICAL);
			}
		}
	}
#endif

	/* make sure the page is of an appropriate size */
	page_len = strlen (page);
	if ((min_page_len > 0) && (page_len < min_page_len)) {
		printf (_("HTTP WARNING: page size too small%s|size=%i\n"),
			(display_html ? "</A>" : ""), page_len );
		exit (STATE_WARNING);
	}
	/* We only get here if all tests have been passed */
	asprintf (&msg, _("HTTP OK %s - %.3f second response time %s%s|time=%ldus size=%dB\n"),
	                status_line, elapsed_time,
	                timestamp, (display_html ? "</A>" : ""), microsec, pagesize);
	die (STATE_OK, "%s", msg);
	return STATE_UNKNOWN;
}




/* per RFC 2396 */
#define HDR_LOCATION "%*[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]: "
#define URI_HTTP "%[HTPShtps]://"
#define URI_HOST "%[-.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define URI_PORT ":%[0123456789]"
#define URI_PATH "%[-_.!~*'();/?:@&=+$,%#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define HD1 URI_HTTP URI_HOST URI_PORT URI_PATH
#define HD2 URI_HTTP URI_HOST URI_PATH
#define HD3 URI_HTTP URI_HOST URI_PORT
#define HD4 URI_HTTP URI_HOST
#define HD5 URI_PATH

int
redir (char *pos, char *status_line)
{
	int i = 0;
	char *x;
	char xx[2];
	char type[6];
	char *addr;
	char port[6];
	char *url;

	addr = malloc (MAX_IPV4_HOSTLENGTH + 1);
	if (addr == NULL)
		die (STATE_UNKNOWN, _("ERROR: could not allocate addr\n"));
	
	url = malloc (strcspn (pos, "\r\n"));
	if (url == NULL)
		die (STATE_UNKNOWN, _("ERROR: could not allocate url\n"));

	while (pos) {

		if (sscanf (pos, "%[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]:%n", xx, &i) < 1) {

			pos += (size_t) strcspn (pos, "\r\n");
			pos += (size_t) strspn (pos, "\r\n");
			if (strlen(pos) == 0) 
				die (STATE_UNKNOWN,
						 _("UNKNOWN - Could not find redirect location - %s%s\n"),
						 status_line, (display_html ? "</A>" : ""));
			continue;
		}

		pos += i;
		pos += strspn (pos, " \t\r\n");

		url = realloc (url, strcspn (pos, "\r\n"));
		if (url == NULL)
			die (STATE_UNKNOWN, _("ERROR: could not allocate url\n"));

		/* URI_HTTP, URI_HOST, URI_PORT, URI_PATH */
		if (sscanf (pos, HD1, type, addr, port, url) == 4) {
			use_ssl = server_type_check (type);
			i = atoi (port);
		}

		/* URI_HTTP URI_HOST URI_PATH */
		else if (sscanf (pos, HD2, type, addr, url) == 3 ) { 
			use_ssl = server_type_check (type);
			i = server_port_check (use_ssl);
		}

		/* URI_HTTP URI_HOST URI_PORT */
		else if(sscanf (pos, HD3, type, addr, port) == 3) {
			strcpy (url, HTTP_URL);
			use_ssl = server_type_check (type);
			i = atoi (port);
		}

		/* URI_HTTP URI_HOST */
		else if(sscanf (pos, HD4, type, addr) == 2) {
			strcpy (url, HTTP_URL);
			use_ssl = server_type_check (type);
			i = server_port_check (use_ssl);
		}

		/* URI_PATH */
		else if (sscanf (pos, HD5, url) == 1) {
			/* relative url */
			if ((url[0] != '/')) {
				if ((x = strrchr(url, '/')))
					*x = '\0';
				asprintf (&server_url, "%s/%s", server_url, url);
			}
			i = server_port;
			strcpy (type, server_type);
			strcpy (addr, host_name);
		} 					

		else {
			die (STATE_UNKNOWN,
					 _("UNKNOWN - Could not parse redirect location - %s%s\n"),
					 pos, (display_html ? "</A>" : ""));
		}

		break;

	} /* end while (pos) */

	if (++redir_depth > max_depth)
		die (STATE_WARNING,
		     _("WARNING - maximum redirection depth %d exceeded - %s://%s:%d%s%s\n"),
		     max_depth, type, addr, i, url, (display_html ? "</A>" : ""));

	if (server_port==i &&
	    !strcmp(server_address, addr) &&
	    (host_name && !strcmp(host_name, addr)) &&
	    !strcmp(server_url, url))
		die (STATE_WARNING,
		     _("WARNING - redirection creates an infinite loop - %s://%s:%d%s%s\n"),
		     type, addr, i, url, (display_html ? "</A>" : ""));

	server_port = i;
	strcpy (server_type, type);

	free (host_name);
	host_name = strdup (addr);

	free (server_address);
	server_address = strdup (addr);

	free (server_url);
	server_url = strdup (url);

	return check_http ();
}



int
server_type_check (const char *type)
{
	if (strcmp (type, "https"))
		return FALSE;
	else
		return TRUE;
}

int
server_port_check (int ssl_flag)
{
	if (ssl_flag)
		return HTTPS_PORT;
	else
		return HTTP_PORT;
}



#ifdef HAVE_SSL
int connect_SSL (void)
{
	SSL_METHOD *meth;

	asprintf (&randbuff, "%s", "qwertyuiopasdfghjklqwertyuiopasdfghjkl");
	RAND_seed (randbuff, (int)strlen(randbuff));
	if (verbose)
		printf(_("SSL seeding: %s\n"), (RAND_status()==1 ? _("OK") : _("Failed")) );

	/* Initialize SSL context */
	SSLeay_add_ssl_algorithms ();
	meth = SSLv23_client_method ();
	SSL_load_error_strings ();
	if ((ctx = SSL_CTX_new (meth)) == NULL) {
		printf (_("CRITICAL -  Cannot create SSL context.\n"));
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
		(timestamp, 17, "%02d/%02d/%04d %02d:%02d",
		 stamp.tm_mon + 1,
		 stamp.tm_mday, stamp.tm_year + 1900, stamp.tm_hour, stamp.tm_min);

	if (days_left > 0 && days_left <= days_till_exp) {
		printf (_("WARNING - Certificate expires in %d day(s) (%s).\n"), days_left, timestamp);
		return STATE_WARNING;
	}
	if (days_left < 0) {
		printf (_("CRITICAL - Certificate expired on %s.\n"), timestamp);
		return STATE_CRITICAL;
	}

	if (days_left == 0) {
		printf (_("WARNING - Certificate expires today (%s).\n"), timestamp);
		return STATE_WARNING;
	}

	printf (_("OK - Certificate will expire on %s.\n"), timestamp);

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
	print_revision (progname, revision);

	printf (_("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n"));
	printf (_(COPYRIGHT), copyright, email);

	printf (_("\
This plugin tests the HTTP service on the specified host. It can test\n\
normal (http) and secure (https) servers, follow redirects, search for\n\
strings and regular expressions, check connection times, and report on\n\
certificate expiration times.\n"));

	print_usage ();

	printf (_("NOTE: One or both of -H and -I must be specified\n"));

	printf (_(UT_HELP_VRSN));

	printf (_("\
 -H, --hostname=ADDRESS\n\
    Host name argument for servers using host headers (virtual host)\n\
 -I, --IP-address=ADDRESS\n\
   IP address or name (use numeric address if possible to bypass DNS lookup).\n\
 -p, --port=INTEGER\n\
   Port number (default: %d)\n"), HTTP_PORT);

	printf (_(UT_IPv46));

#ifdef HAVE_SSL
	printf (_("\
 -S, --ssl\n\
    Connect via SSL\n\
 -C, --certificate=INTEGER\n\
    Minimum number of days a certificate has to be valid.\n\
    (when this option is used the url is not checked.)\n"));
#endif

	printf (_("\
 -e, --expect=STRING\n\
   String to expect in first (status) line of server response (default: %s)\n\
   If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)\n\
 -s, --string=STRING\n\
   String to expect in the content\n\
 -u, --url=PATH\n\
   URL to GET or POST (default: /)\n\
 -P, --post=STRING\n\
   URL encoded http POST data\n"), HTTP_EXPECT);

#ifdef HAVE_REGEX_H
	printf (_("\
 -l, --linespan\n\
    Allow regex to span newlines (must precede -r or -R)\n\
 -r, --regex, --ereg=STRING\n\
    Search page for regex STRING\n\
 -R, --eregi=STRING\n\
    Search page for case-insensitive regex STRING\n"));
#endif

	printf (_("\
 -a, --authorization=AUTH_PAIR\n\
   Username:password on sites with basic authentication\n\
 -L, --link=URL\n\
   Wrap output in HTML link (obsoleted by urlize)\n\
 -f, --onredirect=<ok|warning|critical|follow>\n\
   How to handle redirected pages\n\
 -m, --min=INTEGER\n\
   Minimum page size required (bytes)\n"));

	printf (_(UT_WARN_CRIT));

	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

	printf (_(UT_VERBOSE));

					printf (_("\
This plugin will attempt to open an HTTP connection with the host. Successful\n\
connects return STATE_OK, refusals and timeouts return STATE_CRITICAL, other\n\
errors return STATE_UNKNOWN.  Successful connects, but incorrect reponse\n\
messages from the host result in STATE_WARNING return values.  If you are\n\
checking a virtual server that uses 'host headers' you must supply the FQDN\n\
(fully qualified domain name) as the [host_name] argument.\n"));

#ifdef HAVE_SSL
	printf (_("\n\
This plugin can also check whether an SSL enabled web server is able to\n\
serve content (optionally within a specified time) or whether the X509 \n\
certificate is still valid for the specified number of days.\n"));
	printf (_("\n\
CHECK CONTENT: check_http -w 5 -c 10 --ssl www.verisign.com\n\n\
When the 'www.verisign.com' server returns its content within 5 seconds, a\n\
STATE_OK will be returned. When the server returns its content but exceeds\n\
the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,\n\
a STATE_CRITICAL will be returned.\n\n"));

	printf (_("\
CHECK CERTIFICATE: check_http www.verisign.com -C 14\n\n\
When the certificate of 'www.verisign.com' is valid for more than 14 days, a\n\
STATE_OK is returned. When the certificate is still valid, but for less than\n\
14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when\n\
the certificate is expired.\n"));
#endif

	printf (_(UT_SUPPORT));

}




void
print_usage (void)
{
	printf (_("\
Usage: %s (-H <vhost> | -I <IP-address>) [-u <uri>] [-p <port>]\n\
  [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L]\n\
  [-a auth] [-f <ok | warn | critcal | follow>] [-e <expect>]\n\
  [-s string] [-l] [-r <regex> | -R <case-insensitive regex>]\n\
  [-P string] [-m min_pg_size] [-4|-6]\n"), progname);
	printf (_(UT_HLP_VRS), progname, progname);
}
