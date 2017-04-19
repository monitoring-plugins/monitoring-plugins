/*****************************************************************************
*
* Monitoring check_curl plugin
*
* License: GPL
* Copyright (c) 1999-2017 Monitoring Plugins Development Team
*
* Description:
*
* This file contains the check_curl plugin
*
* This plugin tests the HTTP service on the specified host. It can test
* normal (http) and secure (https) servers, follow redirects, search for
* strings and regular expressions, check connection times, and report on
* certificate expiration times.
*
* This plugin uses functions from the curl library, see
* http://curl.haxx.se
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
*
*****************************************************************************/
const char *progname = "check_curl";

const char *copyright = "2006-2017";
const char *email = "devel@monitoring-plugins.org";

#include <ctype.h>

#include "common.h"
#include "utils.h"

#ifndef LIBCURL_PROTOCOL_HTTP
#error libcurl compiled without HTTP support, compiling check_curl plugin does not makes a lot of sense
#endif

#include "curl/curl.h"
#include "curl/easy.h"

/* TODO: probe this one, how!? */
#define LIBCURL_USES_OPENSSL

#include "picohttpparser.h"

#define MAKE_LIBCURL_VERSION(major, minor, patch) ((major)*0x10000 + (minor)*0x100 + (patch))

#define DEFAULT_BUFFER_SIZE 2048
#define DEFAULT_SERVER_URL "/"
#define HTTP_EXPECT "HTTP/1."
enum {
  HTTP_PORT = 80,
  HTTPS_PORT = 443,
  MAX_PORT = 65535
};

/* for buffers for header and body */
typedef struct {
  char *buf;
  size_t buflen;
  size_t bufsize;
} curlhelp_write_curlbuf;

/* for buffering the data sent in PUT */
typedef struct {
  char *buf;
  size_t buflen;
  off_t pos;
} curlhelp_read_curlbuf;

/* for parsing the HTTP status line */
typedef struct {
  int http_major;   /* major version of the protocol, always 1 (HTTP/0.9
                     * never reached the big internet most likely) */
  int http_minor;   /* minor version of the protocol, usually 0 or 1 */
  int http_code;    /* HTTP return code as in RFC 2145 */
  int http_subcode; /* Microsoft IIS extension, HTTP subcodes, see
                     * http://support.microsoft.com/kb/318380/en-us */
  const char *msg;  /* the human readable message */
  char *first_line; /* a copy of the first line */
} curlhelp_statusline;

enum {
  REGS = 2,
  MAX_RE_SIZE = 256
};
#include "regex.h"
regex_t preg;
regmatch_t pmatch[REGS];
char regexp[MAX_RE_SIZE];
int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
int errcode;
int invert_regex = 0;

char *server_address;
char *host_name;
char *server_url = DEFAULT_SERVER_URL;
unsigned short server_port = HTTP_PORT;
int virtual_port = 0;
int host_name_length;
char output_header_search[30] = "";
char output_string_search[30] = "";
char *warning_thresholds = NULL;
char *critical_thresholds = NULL;
int days_till_exp_warn, days_till_exp_crit;
thresholds *thlds;
char user_agent[DEFAULT_BUFFER_SIZE];
int verbose = 0;
int show_extended_perfdata = FALSE;
int min_page_len = 0;
int max_page_len = 0;
char *http_method = NULL;
char *http_post_data = NULL;
char *http_content_type = NULL;
CURL *curl;
struct curl_slist *header_list = NULL;
curlhelp_write_curlbuf body_buf;
curlhelp_write_curlbuf header_buf;
curlhelp_statusline status_line;
curlhelp_read_curlbuf put_buf;
char http_header[DEFAULT_BUFFER_SIZE];
long code;
long socket_timeout = DEFAULT_SOCKET_TIMEOUT;
double total_time;
double time_connect;
double time_appconnect;
double time_headers;
double time_firstbyte;
char errbuf[CURL_ERROR_SIZE+1];
CURLcode res;
char url[DEFAULT_BUFFER_SIZE];
char msg[DEFAULT_BUFFER_SIZE];
char perfstring[DEFAULT_BUFFER_SIZE];
char header_expect[MAX_INPUT_BUFFER] = "";
char string_expect[MAX_INPUT_BUFFER] = "";
char server_expect[MAX_INPUT_BUFFER] = HTTP_EXPECT;
int server_expect_yn = 0;
char user_auth[MAX_INPUT_BUFFER] = "";
int display_html = FALSE;
int onredirect = STATE_OK;
int use_ssl = FALSE;
int use_sni = TRUE;
int check_cert = FALSE;
union {
  struct curl_slist* to_info;
  struct curl_certinfo* to_certinfo;
} cert_ptr;
int ssl_version = CURL_SSLVERSION_DEFAULT;
char *client_cert = NULL;
char *client_privkey = NULL;
char *ca_cert = NULL;
#ifdef HAVE_SSL
X509 *cert = NULL;
#endif
int no_body = FALSE;
int maximum_age = -1;
int address_family = AF_UNSPEC;

int process_arguments (int, char**);
void handle_curl_option_return_code (CURLcode res, const char* option);
int check_http (void);
void print_help (void);
void print_usage (void);
void print_curl_version (void);
int curlhelp_initwritebuffer (curlhelp_write_curlbuf*);
int curlhelp_buffer_write_callback (void*, size_t , size_t , void*);
void curlhelp_freewritebuffer (curlhelp_write_curlbuf*);
int curlhelp_initreadbuffer (curlhelp_read_curlbuf *, const char *, size_t);
int curlhelp_buffer_read_callback (void *, size_t , size_t , void *);
void curlhelp_freereadbuffer (curlhelp_read_curlbuf *);

int curlhelp_parse_statusline (const char*, curlhelp_statusline *);
void curlhelp_free_statusline (curlhelp_statusline *);
char *perfd_time_ssl (double microsec);
char *get_header_value (const struct phr_header* headers, const size_t nof_headers, const char* header);
static time_t parse_time_string (const char *string);
int check_document_dates (const curlhelp_write_curlbuf *, char (*msg)[DEFAULT_BUFFER_SIZE]);

void remove_newlines (char *);
void test_file (char *);

int
main (int argc, char **argv)
{
  int result = STATE_UNKNOWN;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Parse extra opts if any */
  argv = np_extra_opts (&argc, argv, progname);

  /* set defaults */
  snprintf( user_agent, DEFAULT_BUFFER_SIZE, "%s/v%s (monitoring-plugins %s)",
    progname, NP_VERSION, VERSION);

  /* parse arguments */
  if (process_arguments (argc, argv) == ERROR)
    usage4 (_("Could not parse arguments"));

  if (display_html == TRUE)
    printf ("<A HREF=\"%s://%s:%d%s\" target=\"_blank\">",
      use_ssl ? "https" : "http", host_name ? host_name : server_address,
      server_port, server_url);

  result = check_http ();
  return result;
}

#ifdef HAVE_SSL

int verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  /* TODO: we get all certificates of the chain, so which ones
   * should we test?
   * TODO: is the last certificate always the server certificate?
   */
  cert = X509_STORE_CTX_get_current_cert(x509_ctx);
  return 1;
}

CURLcode sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm)
{
  SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, verify_callback);

  return CURLE_OK;
}

#endif /* HAVE_SSL */

/* Checks if the server 'reply' is one of the expected 'statuscodes' */
static int
expected_statuscode (const char *reply, const char *statuscodes)
{
  char *expected, *code;
  int result = 0;

  if ((expected = strdup (statuscodes)) == NULL)
    die (STATE_UNKNOWN, _("HTTP UNKNOWN - Memory allocation error\n"));

  for (code = strtok (expected, ","); code != NULL; code = strtok (NULL, ","))
    if (strstr (reply, code) != NULL) {
      result = 1;
      break;
    }

  free (expected);
  return result;
}

void
handle_curl_option_return_code (CURLcode res, const char* option)
{
  if (res != CURLE_OK) {
    snprintf (msg, DEFAULT_BUFFER_SIZE, _("Error while setting cURL option '%s': cURL returned %d - %s"),
      option, res, curl_easy_strerror(res));
    die (STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
  }
}

int
check_http (void)
{
  int result = STATE_OK;
  int page_len = 0;

  /* initialize curl */
  if (curl_global_init (CURL_GLOBAL_DEFAULT) != CURLE_OK)
    die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_global_init failed\n");

  if ((curl = curl_easy_init()) == NULL)
    die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_easy_init failed\n");

  if (verbose >= 1)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_VERBOSE, TRUE), "CURLOPT_VERBOSE");

  /* print everything on stdout like check_http would do */
  handle_curl_option_return_code (curl_easy_setopt(curl, CURLOPT_STDERR, stdout), "CURLOPT_STDERR");

  /* initialize buffer for body of the answer */
  if (curlhelp_initwritebuffer(&body_buf) < 0)
    die (STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for body\n");
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)curlhelp_buffer_write_callback), "CURLOPT_WRITEFUNCTION");
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_WRITEDATA, (void *)&body_buf), "CURLOPT_WRITEDATA");

  /* initialize buffer for header of the answer */
  if (curlhelp_initwritebuffer( &header_buf ) < 0)
    die (STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for header\n" );
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, (curl_write_callback)curlhelp_buffer_write_callback), "CURLOPT_HEADERFUNCTION");
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_WRITEHEADER, (void *)&header_buf), "CURLOPT_WRITEHEADER");

  /* set the error buffer */
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errbuf), "CURLOPT_ERRORBUFFER");

  /* set timeouts */
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, socket_timeout), "CURLOPT_CONNECTTIMEOUT");
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_TIMEOUT, socket_timeout), "CURLOPT_TIMEOUT");

  /* compose URL */
  snprintf (url, DEFAULT_BUFFER_SIZE, "%s://%s%s", use_ssl ? "https" : "http",
    server_address, server_url);
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_URL, url), "CURLOPT_URL");

  /* set port */
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_PORT, server_port), "CURLOPT_PORT");

  /* set HTTP method */
  if (http_method) {
    if (!strcmp(http_method, "POST"))
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_POST, 1), "CURLOPT_POST");
    else if (!strcmp(http_method, "PUT"))
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_UPLOAD, 1), "CURLOPT_UPLOAD");
    else
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, http_method), "CURLOPT_CUSTOMREQUEST");
  }

  /* set hostname (virtual hosts) */
  if(host_name != NULL) {
    if((virtual_port != HTTP_PORT && !use_ssl) || (virtual_port != HTTPS_PORT && use_ssl)) {
      snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s:%d", host_name, virtual_port);
    } else {
      snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s", host_name);
    }
    header_list = curl_slist_append (header_list, http_header);
  }

  /* always close connection, be nice to servers */
  snprintf (http_header, DEFAULT_BUFFER_SIZE, "Connection: close");
  header_list = curl_slist_append (header_list, http_header);

  /* set HTTP headers */
  handle_curl_option_return_code (curl_easy_setopt( curl, CURLOPT_HTTPHEADER, header_list ), "CURLOPT_HTTPHEADER");

#ifdef LIBCURL_FEATURE_SSL

  /* set SSL version, warn about unsecure or unsupported versions */
  if (use_ssl) {
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_SSLVERSION, ssl_version), "CURLOPT_SSLVERSION");
  }

  /* client certificate and key to present to server (SSL) */
  if (client_cert)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_SSLCERT, client_cert), "CURLOPT_SSLCERT");
  if (client_privkey)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_SSLKEY, client_privkey), "CURLOPT_SSLKEY");
  if (ca_cert) {
    /* per default if we have a CA verify both the peer and the
     * hostname in the certificate, can be switched off later */
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_CAINFO, ca_cert), "CURLOPT_CAINFO");
    handle_curl_option_return_code (curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 1), "CURLOPT_SSL_VERIFYPEER");
    handle_curl_option_return_code (curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 2), "CURLOPT_SSL_VERIFYHOST");
  } else {  
    /* backward-compatible behaviour, be tolerant in checks
     * TODO: depending on more options have aspects we want
     * to be less tolerant about ssl verfications
     */
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0), "CURLOPT_SSL_VERIFYPEER");
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0), "CURLOPT_SSL_VERIFYHOST");
  }

  /* try hard to get a stack of certificates to verify against */
  if (check_cert)
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1)
    /* inform curl to report back certificates (this works for OpenSSL, NSS at least) */
    curl_easy_setopt (curl, CURLOPT_CERTINFO, 1L);
#ifdef LIBCURL_USES_OPENSSL
    /* set callback to extract certificate with OpenSSL context function (works with
     * OpenSSL only!)
     */
    handle_curl_option_return_code (curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun), "CURLOPT_SSL_CTX_FUNCTION");
#endif /* USE_OPENSSL */
#else /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */
#ifdef LIBCURL_USES_OPENSSL
    /* Too old curl library, hope we have OpenSSL */
    handle_curl_option_return_code (curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun), "CURLOPT_SSL_CTX_FUNCTION");
#else
    die (STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (no CURLOPT_SSL_CTX_FUNCTION, no OpenSSL library)\n");
#endif /* LIBCURL_USES_OPENSSL */
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */

#endif /* LIBCURL_FEATURE_SSL */

  /* set default or user-given user agent identification */
  handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_USERAGENT, user_agent), "CURLOPT_USERAGENT");

  /* authentication */
  if (strcmp(user_auth, ""))
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_USERPWD, user_auth), "CURLOPT_USERPWD");

  /* TODO: parameter auth method, bitfield of following methods:
   * CURLAUTH_BASIC (default)
   * CURLAUTH_DIGEST
   * CURLAUTH_DIGEST_IE
   * CURLAUTH_NEGOTIATE
   * CURLAUTH_NTLM
   * CURLAUTH_NTLM_WB
   *
   * convenience tokens for typical sets of methods:
   * CURLAUTH_ANYSAFE: most secure, without BASIC
   * or CURLAUTH_ANY: most secure, even BASIC if necessary
   *
   * handle_curl_option_return_code (curl_easy_setopt( curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_DIGEST ), "CURLOPT_HTTPAUTH");
   */

  /* handle redirections */
  if (onredirect == STATE_DEPENDENT) {
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1), "CURLOPT_FOLLOWLOCATION");
    /* TODO: handle the following aspects of redirection
      CURLOPT_POSTREDIR: method switch
      CURLINFO_REDIRECT_URL: custom redirect option
      CURLOPT_REDIRECT_PROTOCOLS
      CURLINFO_REDIRECT_COUNT
    */
  }

  /* no-body */
  if (no_body)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_NOBODY, 1), "CURLOPT_NOBODY");

  /* IPv4 or IPv6 forced DNS resolution */
  if (address_family == AF_UNSPEC)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER), "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_WHATEVER)");
  else if (address_family == AF_INET)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4), "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V4)");
#if defined (USE_IPV6) && defined (LIBCURL_FEATURE_IPV6)
  else if (address_family == AF_INET6)
    handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6), "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V6)");
#endif

  /* either send http POST data (any data, not only POST)*/
  if (!strcmp(http_method, "POST") ||!strcmp(http_method, "PUT")) {
    /* set content of payload for POST and PUT */
    if (http_content_type) {
      snprintf (http_header, DEFAULT_BUFFER_SIZE, "Content-Type: %s", http_content_type);
      header_list = curl_slist_append (header_list, http_header);
    }
    /* NULL indicates "HTTP Continue" in libcurl, provide an empty string
     * in case of no POST/PUT data */
    if (!http_post_data)
      http_post_data = "";
    if (!strcmp(http_method, "POST")) {
      /* POST method, set payload with CURLOPT_POSTFIELDS */
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_POSTFIELDS, http_post_data), "CURLOPT_POSTFIELDS");
    } else if (!strcmp(http_method, "PUT")) {
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_READFUNCTION, (curl_read_callback)curlhelp_buffer_read_callback), "CURLOPT_READFUNCTION");
      curlhelp_initreadbuffer (&put_buf, http_post_data, strlen (http_post_data));
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_READDATA, (void *)&put_buf), "CURLOPT_READDATA");
      handle_curl_option_return_code (curl_easy_setopt (curl, CURLOPT_INFILESIZE, (curl_off_t)strlen (http_post_data)), "CURLOPT_INFILESIZE");
    }
  }

  /* do the request */
  res = curl_easy_perform(curl);

  if (verbose>=2 && http_post_data)
    printf ("**** REQUEST CONTENT ****\n%s\n", http_post_data);

  /* free header list, we don't need it anymore */
  curl_slist_free_all(header_list);

  /* Curl errors, result in critical Nagios state */
  if (res != CURLE_OK) {
    snprintf (msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host on port %d: cURL returned %d - %s"),
      server_port, res, curl_easy_strerror(res));
    die (STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
  }

  /* certificate checks */
#ifdef LIBCURL_FEATURE_SSL
  if (use_ssl == TRUE) {
    if (check_cert == TRUE) {
      if (verbose >= 2)
        printf ("**** REQUEST CERTIFICATES ****\n");
      cert_ptr.to_info = NULL;
      res = curl_easy_getinfo (curl, CURLINFO_CERTINFO, &cert_ptr.to_info);
      if (!res && cert_ptr.to_info) {
        int i;
        for (i = 0; i < cert_ptr.to_certinfo->num_of_certs; i++) {
          struct curl_slist *slist;
          for (slist = cert_ptr.to_certinfo->certinfo[i]; slist; slist = slist->next) {
            if (verbose >= 2)
              printf ("%d ** %s\n", i, slist->data);
          }
        }
      } else {
        snprintf (msg, DEFAULT_BUFFER_SIZE, _("Cannot retrieve certificates - cURL returned %d - %s"),
          res, curl_easy_strerror(res));
        die (STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
      }
      if (verbose >= 2)
        printf ("**** REQUEST CERTIFICATES ****\n");
      /* check certificate with OpenSSL functions, curl has been built against OpenSSL
       * and we actually have OpenSSL in the monitoring tools
       */
#ifdef HAVE_SSL
#ifdef LIBCURL_USES_OPENSSL
      result = np_net_ssl_check_certificate(cert, days_till_exp_warn, days_till_exp_crit);
#endif /* LIBCURL_USES_OPENSSL */
#endif /* HAVE_SSL */
      return result;
    }
  }
#endif /* LIBCURL_FEATURE_SSL */

  /* we got the data and we executed the request in a given time, so we can append
   * performance data to the answer always
   */
  handle_curl_option_return_code (curl_easy_getinfo (curl, CURLINFO_TOTAL_TIME, &total_time), "CURLINFO_TOTAL_TIME");
  if(show_extended_perfdata) {
    handle_curl_option_return_code (curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &time_connect), "CURLINFO_CONNECT_TIME");
    handle_curl_option_return_code (curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &time_appconnect), "CURLINFO_APPCONNECT_TIME");
    handle_curl_option_return_code (curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &time_headers), "CURLINFO_PRETRANSFER_TIME");
    handle_curl_option_return_code (curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &time_firstbyte), "CURLINFO_STARTTRANSFER_TIME");
    snprintf(perfstring, DEFAULT_BUFFER_SIZE, "time=%.6gs;%.6g;%.6g;; size=%dB;;; time_connect=%.6gs;;;; %s time_headers=%.6gs;;;; time_firstbyte=%.6gs;;;; time_transfer=%.6gs;;;;",
      total_time,
      warning_thresholds != NULL ? (double)thlds->warning->end : 0.0,
      critical_thresholds != NULL ? (double)thlds->critical->end : 0.0,
      (int)body_buf.buflen,
      time_connect,
      use_ssl == TRUE ? perfd_time_ssl(time_appconnect-time_connect) : "",
      (time_headers - time_appconnect),
      (time_firstbyte - time_headers),
      (total_time-time_firstbyte)
      );
  } else {
    snprintf(perfstring, DEFAULT_BUFFER_SIZE, "time=%.6gs;%.6g;%.6g;; size=%dB;;;",
      total_time,
      warning_thresholds != NULL ? (double)thlds->warning->end : 0.0,
      critical_thresholds != NULL ? (double)thlds->critical->end : 0.0,
      (int)body_buf.buflen);
  }

  /* return a CRITICAL status if we couldn't read any data */
  if (strlen(header_buf.buf) == 0 && strlen(body_buf.buf) == 0)
    die (STATE_CRITICAL, _("HTTP CRITICAL - No header received from host\n"));

  /* get status line of answer, check sanity of HTTP code */
  if (curlhelp_parse_statusline (header_buf.buf, &status_line) < 0) {
    snprintf (msg, DEFAULT_BUFFER_SIZE, "Unparseable status line in %.3g seconds response time|%s\n",
      code, total_time, perfstring);
    die (STATE_CRITICAL, "HTTP CRITICAL HTTP/1.x %d unknown - %s", code, msg);
  }

  /* get result code from cURL */
  handle_curl_option_return_code (curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &code), "CURLINFO_RESPONSE_CODE");
  if (verbose>=2)
    printf ("* curl CURLINFO_RESPONSE_CODE is %d\n", code);

  /* print status line, header, body if verbose */
  if (verbose >= 2) {
    printf ("**** HEADER ****\n%s\n**** CONTENT ****\n%s\n", header_buf.buf,
                (no_body ? "  [[ skipped ]]" : body_buf.buf));
  }

  /* make sure the status line matches the response we are looking for */
  if (!expected_statuscode(status_line.first_line, server_expect)) {
    /* TODO: fix first_line being cut off */
    if (server_port == HTTP_PORT)
      snprintf(msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host: %s\n"), status_line.first_line);
    else
      snprintf(msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host on port %d: %s\n"), server_port, status_line.first_line);
    die (STATE_CRITICAL, "HTTP CRITICAL - %s", msg);
  }

  /* TODO: implement -d header tests */
  if( server_expect_yn  )  {
    snprintf(msg, DEFAULT_BUFFER_SIZE, _("Status line output matched \"%s\" - "), server_expect);
    if (verbose)
      printf ("%s\n",msg);
    result = STATE_OK;
  }
  else {
    /* illegal return codes result in a critical state */
    if (code >= 600 || code < 100) {
      die (STATE_CRITICAL, _("HTTP CRITICAL: Invalid Status (%d, %.40s)\n"), status_line.http_code, status_line.msg);
    /* server errors result in a critical state */
        } else if (code >= 500) {
      result = STATE_CRITICAL;
        /* client errors result in a warning state */
    } else if (code >= 400) {
      result = STATE_WARNING;
    /* check redirected page if specified */
    } else if (code >= 300) {
      if (onredirect == STATE_DEPENDENT) {
        code = status_line.http_code;
      }
      result = max_state_alt (onredirect, result);
      /* TODO: make sure the last status line has been
         parsed into the status_line structure
       */
    /* all other codes are considered ok */
    } else {
      result = STATE_OK;
    }
  }

  /* check status codes, set exit status accordingly */
  if( status_line.http_code != code ) {
    die (STATE_CRITICAL, _("HTTP CRITICAL HTTP/%d.%d %d %s - different HTTP codes (cUrl has %ld)\n"),
      status_line.http_major, status_line.http_minor,
      status_line.http_code, status_line.msg, code);
  }

  if (maximum_age >= 0) {
    result = max_state_alt(check_document_dates(&header_buf, &msg), result);
  }

  /* Page and Header content checks go here */

  if (strlen (header_expect)) {
    if (!strstr (header_buf.buf, header_expect)) {
      strncpy(&output_header_search[0],header_expect,sizeof(output_header_search));
      if(output_header_search[sizeof(output_header_search)-1]!='\0') {
        bcopy("...",&output_header_search[sizeof(output_header_search)-4],4);
      }
      snprintf (msg, DEFAULT_BUFFER_SIZE, _("%sheader '%s' not found on '%s://%s:%d%s', "), msg, output_header_search, use_ssl ? "https" : "http", host_name ? host_name : server_address, server_port, server_url);
      result = STATE_CRITICAL;
    }
  }

  if (strlen (string_expect)) {
    if (!strstr (body_buf.buf, string_expect)) {
      strncpy(&output_string_search[0],string_expect,sizeof(output_string_search));
      if(output_string_search[sizeof(output_string_search)-1]!='\0') {
        bcopy("...",&output_string_search[sizeof(output_string_search)-4],4);
      }
      snprintf (msg, DEFAULT_BUFFER_SIZE, _("%sstring '%s' not found on '%s://%s:%d%s', "), msg, output_string_search, use_ssl ? "https" : "http", host_name ? host_name : server_address, server_port, server_url);
      result = STATE_CRITICAL;
    }
  }

  if (strlen (regexp)) {
    errcode = regexec (&preg, body_buf.buf, REGS, pmatch, 0);
    if ((errcode == 0 && invert_regex == 0) || (errcode == REG_NOMATCH && invert_regex == 1)) {
      /* OK - No-op to avoid changing the logic around it */
      result = max_state_alt(STATE_OK, result);
    }
    else if ((errcode == REG_NOMATCH && invert_regex == 0) || (errcode == 0 && invert_regex == 1)) {
      if (invert_regex == 0)
        snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spattern not found, "), msg);
      else
        snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spattern found, "), msg);
      result = STATE_CRITICAL;
    }
    else {
      /* FIXME: Shouldn't that be UNKNOWN? */
      regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
      snprintf (msg, DEFAULT_BUFFER_SIZE, _("%sExecute Error: %s, "), msg, errbuf);
      result = STATE_CRITICAL;
    }
  }

  /* make sure the page is of an appropriate size
   * TODO: as far I can tell check_http gets the full size of header and
   * if -N is not given header+body. Does this make sense?
   * 
   * TODO: check_http.c had a get_length function, the question is really
   * here what to use? the raw data size of the header_buf, the value of
   * Content-Length, both and warn if they differ? Should the length be
   * header+body or only body?
   * 
   * One possible policy:
   * - use header_buf.buflen (warning, if it mismatches to the Content-Length value
   * - if -N (nobody) is given, use Content-Length only and hope the server set
   *   the value correcly
   */
  page_len = header_buf.buflen + body_buf.buflen;
  if ((max_page_len > 0) && (page_len > max_page_len)) {
    snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spage size %d too large, "), msg, page_len);
    result = max_state_alt(STATE_WARNING, result);
  } else if ((min_page_len > 0) && (page_len < min_page_len)) {
    snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spage size %d too small, "), msg, page_len);
    result = max_state_alt(STATE_WARNING, result);
  }

  /* -w, -c: check warning and critical level */
  result = max_state_alt(get_status(total_time, thlds), result);

  /* Cut-off trailing characters */
  if(msg[strlen(msg)-2] == ',')
    msg[strlen(msg)-2] = '\0';
  else
    msg[strlen(msg)-3] = '\0';

  /* TODO: separate _() msg and status code: die (result, "HTTP %s: %s\n", state_text(result), msg); */
  die (result, "HTTP %s: HTTP/%d.%d %d %s%s%s - %d bytes in %.3f second response time %s|%s\n",
    state_text(result), status_line.http_major, status_line.http_minor,
    status_line.http_code, status_line.msg,
    strlen(msg) > 0 ? " - " : "",
    msg, page_len, total_time,
    (display_html ? "</A>" : ""),
    perfstring);

  /* proper cleanup after die? */
  curlhelp_free_statusline(&status_line);
  curl_easy_cleanup (curl);
  curl_global_cleanup ();
  curlhelp_freewritebuffer (&body_buf);
  curlhelp_freewritebuffer (&header_buf);
  if (!strcmp (http_method, "PUT")) { 
    curlhelp_freereadbuffer (&put_buf);
  }

  return result;
}

/* check whether a file exists */
void
test_file (char *path)
{
  if (access(path, R_OK) == 0)
    return;
  usage2 (_("file does not exist or is not readable"), path);
}

int
process_arguments (int argc, char **argv)
{
  char *p;
  int c = 1;
  char *temp;

  enum {
    INVERT_REGEX = CHAR_MAX + 1,
    SNI_OPTION,
    CA_CERT_OPTION
  };

  int option = 0;
  int got_plus = 0;
  static struct option longopts[] = {
    STD_LONG_OPTS,
    {"link", no_argument, 0, 'L'},
    {"nohtml", no_argument, 0, 'n'},
    {"ssl", optional_argument, 0, 'S'},
    {"sni", no_argument, 0, SNI_OPTION},
    {"post", required_argument, 0, 'P'},
    {"method", required_argument, 0, 'j'},
    {"IP-address", required_argument, 0, 'I'},
    {"url", required_argument, 0, 'u'},
    {"port", required_argument, 0, 'p'},
    {"authorization", required_argument, 0, 'a'},
    {"header-string", required_argument, 0, 'd'},
    {"string", required_argument, 0, 's'},
    {"expect", required_argument, 0, 'e'},
    {"regex", required_argument, 0, 'r'},
    {"ereg", required_argument, 0, 'r'},
    {"eregi", required_argument, 0, 'R'},
    {"linespan", no_argument, 0, 'l'},
    {"onredirect", required_argument, 0, 'f'},
    {"certificate", required_argument, 0, 'C'},
    {"client-cert", required_argument, 0, 'J'},
    {"private-key", required_argument, 0, 'K'},
    {"ca-cert", required_argument, 0, CA_CERT_OPTION},
    {"useragent", required_argument, 0, 'A'},
    {"header", required_argument, 0, 'k'},
    {"no-body", no_argument, 0, 'N'},
    {"max-age", required_argument, 0, 'M'},
    {"content-type", required_argument, 0, 'T'},
    {"pagesize", required_argument, 0, 'm'},
    {"invert-regex", no_argument, NULL, INVERT_REGEX},
    {"use-ipv4", no_argument, 0, '4'},
    {"use-ipv6", no_argument, 0, '6'},
    {"extended-perfdata", no_argument, 0, 'E'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  /* support check_http compatible arguments */
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
    c = getopt_long (argc, argv, "Vvh46t:c:w:A:k:H:P:j:T:I:a:p:d:e:s:R:r:u:f:C:J:K:nLS::m:M:NE", longopts, &option);
    if (c == -1 || c == EOF || c == 1)
      break;

    switch (c) {
    case 'h':
      print_help();
      exit(STATE_UNKNOWN);
      break;
    case 'V':
      print_revision(progname, NP_VERSION);
      print_curl_version();
      exit(STATE_UNKNOWN);
      break;
    case 'v':
      verbose++;
      break;
    case 't': /* timeout period */
      if (!is_intnonneg (optarg))
        usage2 (_("Timeout interval must be a positive integer"), optarg);
      else
        socket_timeout = (int)strtol (optarg, NULL, 10);
      break;
    case 'c': /* critical time threshold */
      critical_thresholds = optarg;
      break;
    case 'w': /* warning time threshold */
      warning_thresholds = optarg;
      break;
    case 'H': /* virtual host */
      host_name = strdup (optarg);
      if (host_name[0] == '[') {
        if ((p = strstr (host_name, "]:")) != NULL) { /* [IPv6]:port */
          virtual_port = atoi (p + 2);
          /* cut off the port */
          host_name_length = strlen (host_name) - strlen (p) - 1;
          free (host_name);
          host_name = strndup (optarg, host_name_length);
          }
      } else if ((p = strchr (host_name, ':')) != NULL
                 && strchr (++p, ':') == NULL) { /* IPv4:port or host:port */
          virtual_port = atoi (p);
          /* cut off the port */
          host_name_length = strlen (host_name) - strlen (p) - 1;
          free (host_name);
          host_name = strndup (optarg, host_name_length);
        }
      break;
    case 'I': /* internet address */
      server_address = strdup (optarg);
      break;
    case 'u': /* URL path */
      server_url = strdup (optarg);
      break;
    case 'p': /* Server port */
      if (!is_intnonneg (optarg))
        usage2 (_("Invalid port number, expecting a non-negative number"), optarg);
      else {
        if( strtol(optarg, NULL, 10) > MAX_PORT)
          usage2 (_("Invalid port number, supplied port number is too big"), optarg);
        server_port = (unsigned short)strtol(optarg, NULL, 10);
      }
      break;
    case 'a': /* authorization info */
      strncpy (user_auth, optarg, MAX_INPUT_BUFFER - 1);
      user_auth[MAX_INPUT_BUFFER - 1] = 0;
      break;
    case 'P': /* HTTP POST data in URL encoded format; ignored if settings already */
      if (! http_post_data)
        http_post_data = strdup (optarg);
      if (! http_method)
        http_method = strdup("POST");
      break;
    case 'j': /* Set HTTP method */
      if (http_method)
        free(http_method);
      http_method = strdup (optarg);
      break;
    case 'A': /* useragent */
      snprintf (user_agent, DEFAULT_BUFFER_SIZE, optarg);
      break;
    case 'k': /* Additional headers */
      header_list = curl_slist_append(header_list, optarg);
      break;
    case 'L': /* show html link */
      display_html = TRUE;
      break;
    case 'n': /* do not show html link */
      display_html = FALSE;
      break;
    case 'C': /* Check SSL cert validity */
#ifdef LIBCURL_FEATURE_SSL
      if ((temp=strchr(optarg,','))!=NULL) {
        *temp='\0';
        if (!is_intnonneg (optarg))
          usage2 (_("Invalid certificate expiration period"), optarg);
        days_till_exp_warn = atoi(optarg);
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
      goto enable_ssl;
#endif
    case 'J': /* use client certificate */
#ifdef LIBCURL_FEATURE_SSL
      test_file(optarg);
      client_cert = optarg;
      goto enable_ssl;
#endif
    case 'K': /* use client private key */
#ifdef LIBCURL_FEATURE_SSL
      test_file(optarg);
      client_privkey = optarg;
      goto enable_ssl;
#endif
#ifdef LIBCURL_FEATURE_SSL
    case CA_CERT_OPTION: /* use CA chain file */
      test_file(optarg);
      ca_cert = optarg;
      goto enable_ssl;
#endif
    case 'S': /* use SSL */
#ifdef LIBCURL_FEATURE_SSL
    enable_ssl:
      use_ssl = TRUE;
      /* ssl_version initialized to CURL_SSLVERSION_TLSv1_0 as a default.
       * Only set if it's non-zero.  This helps when we include multiple
       * parameters, like -S and -C combinations */
      ssl_version = CURL_SSLVERSION_TLSv1_0;
      if (c=='S' && optarg != NULL) {
        char *plus_ptr = strchr(optarg, '+');
        if (plus_ptr) {
          got_plus = 1;
          *plus_ptr = '\0';
        }
        
        if (optarg[0] == '2')
          ssl_version = CURL_SSLVERSION_SSLv2;
        else if (optarg[0] == '3')
          ssl_version = CURL_SSLVERSION_SSLv3;
        else if (!strcmp (optarg, "1") || !strcmp (optarg, "1.0"))
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
          ssl_version = CURL_SSLVERSION_TLSv1_0;
#else
          ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
        else if (!strcmp (optarg, "1.1"))
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
          ssl_version = CURL_SSLVERSION_TLSv1_1;
#else
          ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
        else if (!strcmp (optarg, "1.2"))
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
          ssl_version = CURL_SSLVERSION_TLSv1_2;
#else
          ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
        else if (!strcmp (optarg, "1.3"))
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0)
          ssl_version = CURL_SSLVERSION_TLSv1_3;
#else
          ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0) */
        else
          usage4 (_("Invalid option - Valid SSL/TLS versions: 2, 3, 1, 1.1, 1.2 (with optional '+' suffix)"));
      }
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0)
      if (got_plus) {
        switch (ssl_version) {
          case CURL_SSLVERSION_TLSv1_3:
            ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
            break;
          case CURL_SSLVERSION_TLSv1_2:
          case CURL_SSLVERSION_TLSv1_1:
          case CURL_SSLVERSION_TLSv1_0:
            ssl_version |= CURL_SSLVERSION_MAX_DEFAULT;
            break;
        }
      } else {
        switch (ssl_version) {
          case CURL_SSLVERSION_TLSv1_3:
            ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
            break;
          case CURL_SSLVERSION_TLSv1_2:
            ssl_version |= CURL_SSLVERSION_MAX_TLSv1_2;
            break;
          case CURL_SSLVERSION_TLSv1_1:
            ssl_version |= CURL_SSLVERSION_MAX_TLSv1_1;
            break;
          case CURL_SSLVERSION_TLSv1_0:
            ssl_version |= CURL_SSLVERSION_MAX_TLSv1_0;
            break;
        }
      }
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0) */ 
      if (verbose >= 2)
        printf(_("* Set SSL/TLS version to %d\n"), ssl_version);
      if (server_port == HTTP_PORT)
        server_port = HTTPS_PORT;
      break;
#else /* LIBCURL_FEATURE_SSL */
      /* -C -J and -K fall through to here without SSL */
      usage4 (_("Invalid option - SSL is not available"));
      break;
    case SNI_OPTION: /* --sni is parsed, but ignored, the default is TRUE with libcurl */
      use_sni = TRUE;
      break;
#endif /* LIBCURL_FEATURE_SSL */
    case 'f': /* onredirect */
      if (!strcmp (optarg, "ok"))
        onredirect = STATE_OK;
      else if (!strcmp (optarg, "warning"))
        onredirect = STATE_WARNING;
      else if (!strcmp (optarg, "critical"))
        onredirect = STATE_CRITICAL;
      else if (!strcmp (optarg, "unknown"))
        onredirect = STATE_UNKNOWN;
      else if (!strcmp (optarg, "follow"))
        onredirect = STATE_DEPENDENT;
      else usage2 (_("Invalid onredirect option"), optarg);
      if (verbose >= 2)
        printf(_("* Following redirects set to %s\n"), state_text(onredirect));
      break;
    case 'd': /* string or substring */
      strncpy (header_expect, optarg, MAX_INPUT_BUFFER - 1);
      header_expect[MAX_INPUT_BUFFER - 1] = 0;
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
    case 'T': /* Content-type */
      http_content_type = strdup (optarg);
     break;
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
    case INVERT_REGEX:
      invert_regex = 1;
      break;
    case '4':
      address_family = AF_INET;
      break;
    case '6':
#if defined (USE_IPV6) && defined (LIBCURL_FEATURE_IPV6)
      address_family = AF_INET6;
#else
      usage4 (_("IPv6 support not available"));
#endif
      break;
    case 'm': /* min_page_length */
      {
      char *tmp;
      if (strchr(optarg, ':') != (char *)NULL) {
        /* range, so get two values, min:max */
        tmp = strtok(optarg, ":");
        if (tmp == NULL) {
          printf("Bad format: try \"-m min:max\"\n");
          exit (STATE_WARNING);
        } else
          min_page_len = atoi(tmp);

        tmp = strtok(NULL, ":");
        if (tmp == NULL) {
          printf("Bad format: try \"-m min:max\"\n");
          exit (STATE_WARNING);
        } else
          max_page_len = atoi(tmp);
      } else
        min_page_len = atoi (optarg);
      break;
      }
    case 'N': /* no-body */
      no_body = TRUE;
      break;
    case 'M': /* max-age */
    {
      int L = strlen(optarg);
      if (L && optarg[L-1] == 'm')
        maximum_age = atoi (optarg) * 60;
      else if (L && optarg[L-1] == 'h')
        maximum_age = atoi (optarg) * 60 * 60;
      else if (L && optarg[L-1] == 'd')
        maximum_age = atoi (optarg) * 60 * 60 * 24;
      else if (L && (optarg[L-1] == 's' ||
                     isdigit (optarg[L-1])))
        maximum_age = atoi (optarg);
      else {
        fprintf (stderr, "unparsable max-age: %s\n", optarg);
        exit (STATE_WARNING);
      }
      if (verbose >= 2)
        printf ("* Maximal age of document set to %d seconds\n", maximum_age);
    }
    break;
    case 'E': /* show extended perfdata */
      show_extended_perfdata = TRUE;
      break;
    case '?':
      /* print short usage statement if args not parsable */
      usage5 ();
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
      usage4 (_("You must specify a server address or host name"));
    else
      server_address = strdup (host_name);
  }

  set_thresholds(&thlds, warning_thresholds, critical_thresholds);

  if (critical_thresholds && thlds->critical->end>(double)socket_timeout)
    socket_timeout = (int)thlds->critical->end + 1;
  if (verbose >= 2)
    printf ("* Socket timeout set to %d seconds\n", socket_timeout);

  if (http_method == NULL)
    http_method = strdup ("GET");

  if (client_cert && !client_privkey)
    usage4 (_("If you use a client certificate you must also specify a private key file"));

  if (virtual_port == 0)
    virtual_port = server_port;

  return TRUE;
}

void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf ("Copyright (c) 2017 Andreas Baumann <mail@andreasbaumann.cc>\n");
  printf (COPYRIGHT, copyright, email);

  printf ("%s\n", _("This plugin tests the HTTP service on the specified host. It can test"));
  printf ("%s\n", _("normal (http) and secure (https) servers, follow redirects, search for"));
  printf ("%s\n", _("strings and regular expressions, check connection times, and report on"));
  printf ("%s\n", _("certificate expiration times."));
  printf ("\n");
  printf ("%s\n", _("It makes use of libcurl to do so. It tries to be as compatible to check_http"));
  printf ("%s\n", _("as possible."));

  printf ("\n\n");

  print_usage ();

  printf (_("NOTE: One or both of -H and -I must be specified"));

  printf ("\n");

  printf (UT_HELP_VRSN);
  printf (UT_EXTRA_OPTS);

  printf (" %s\n", "-H, --hostname=ADDRESS");
  printf ("    %s\n", _("Host name argument for servers using host headers (virtual host)"));
  printf ("    %s\n", _("Append a port to include it in the header (eg: example.com:5000)"));
  printf (" %s\n", "-I, --IP-address=ADDRESS");
  printf ("    %s\n", _("IP address or name (use numeric address if possible to bypass DNS lookup)."));
  printf (" %s\n", "-p, --port=INTEGER");
  printf ("    %s", _("Port number (default: "));
  printf ("%d)\n", HTTP_PORT);

  printf (UT_IPv46);

#ifdef LIBCURL_FEATURE_SSL
  printf (" %s\n", "-S, --ssl=VERSION[+]");
  printf ("    %s\n", _("Connect via SSL. Port defaults to 443. VERSION is optional, and prevents"));
  printf ("    %s\n", _("auto-negotiation (2 = SSLv2, 3 = SSLv3, 1 = TLSv1, 1.1 = TLSv1.1,"));
  printf ("    %s\n", _("1.2 = TLSv1.2). With a '+' suffix, newer versions are also accepted."));
  printf ("    %s\n", _("Note: SSLv2 and SSLv3 are deprecated and are usually disabled in libcurl"));
  printf (" %s\n", "--sni");
  printf ("    %s\n", _("Enable SSL/TLS hostname extension support (SNI)"));
#if LIBCURL_VERSION_NUM >= 0x071801
  printf ("    %s\n", _("Note: --sni is the default in libcurl as SSLv2 and SSLV3 are deprecated and"));
  printf ("    %s\n", _("      SNI only really works since TLSv1.0"));
#else
  printf ("    %s\n", _("Note: SNI is not supported in libcurl before 7.18.1"));
#endif
  printf (" %s\n", "-C, --certificate=INTEGER[,INTEGER]");
  printf ("    %s\n", _("Minimum number of days a certificate has to be valid. Port defaults to 443"));
  printf ("    %s\n", _("(when this option is used the URL is not checked.)"));
  printf (" %s\n", "-J, --client-cert=FILE");
  printf ("   %s\n", _("Name of file that contains the client certificate (PEM format)"));
  printf ("   %s\n", _("to be used in establishing the SSL session"));
  printf (" %s\n", "-K, --private-key=FILE");
  printf ("   %s\n", _("Name of file containing the private key (PEM format)"));
  printf ("   %s\n", _("matching the client certificate"));
  printf (" %s\n", "--ca-cert=FILE");
  printf ("   %s\n", _("CA certificate file to verify peer against"));
#endif

  printf (" %s\n", "-e, --expect=STRING");
  printf ("    %s\n", _("Comma-delimited list of strings, at least one of them is expected in"));
  printf ("    %s", _("the first (status) line of the server response (default: "));
  printf ("%s)\n", HTTP_EXPECT);
  printf ("    %s\n", _("If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)"));
  printf (" %s\n", "-d, --header-string=STRING");
  printf ("    %s\n", _("String to expect in the response headers"));
  printf (" %s\n", "-s, --string=STRING");
  printf ("    %s\n", _("String to expect in the content"));
  printf (" %s\n", "-u, --url=PATH");
  printf ("    %s\n", _("URL to GET or POST (default: /)"));
  printf (" %s\n", "-P, --post=STRING");
  printf ("    %s\n", _("URL encoded http POST data"));
  printf (" %s\n", "-j, --method=STRING  (for example: HEAD, OPTIONS, TRACE, PUT, DELETE, CONNECT)");
  printf ("    %s\n", _("Set HTTP method."));
  printf (" %s\n", "-N, --no-body");
  printf ("    %s\n", _("Don't wait for document body: stop reading after headers."));
  printf ("    %s\n", _("(Note that this still does an HTTP GET or POST, not a HEAD.)"));
  printf (" %s\n", "-M, --max-age=SECONDS");
  printf ("    %s\n", _("Warn if document is more than SECONDS old. the number can also be of"));
  printf ("    %s\n", _("the form \"10m\" for minutes, \"10h\" for hours, or \"10d\" for days."));
  printf (" %s\n", "-T, --content-type=STRING");
  printf ("    %s\n", _("specify Content-Type header media type when POSTing\n"));
  printf (" %s\n", "-l, --linespan");
  printf ("    %s\n", _("Allow regex to span newlines (must precede -r or -R)"));
  printf (" %s\n", "-r, --regex, --ereg=STRING");
  printf ("    %s\n", _("Search page for regex STRING"));
  printf (" %s\n", "-R, --eregi=STRING");
  printf ("    %s\n", _("Search page for case-insensitive regex STRING"));
  printf (" %s\n", "--invert-regex");
  printf ("    %s\n", _("Return CRITICAL if found, OK if not\n"));
  printf (" %s\n", "-a, --authorization=AUTH_PAIR");
  printf ("    %s\n", _("Username:password on sites with basic authentication"));
  printf (" %s\n", "-A, --useragent=STRING");
  printf ("    %s\n", _("String to be sent in http header as \"User Agent\""));
  printf (" %s\n", "-k, --header=STRING");
  printf ("    %s\n", _("Any other tags to be sent in http header. Use multiple times for additional headers"));
  printf (" %s\n", "-E, --extended-perfdata");
  printf ("    %s\n", _("Print additional performance data"));
  printf (" %s\n", "-L, --link");
  printf ("    %s\n", _("Wrap output in HTML link (obsoleted by urlize)"));
  printf (" %s\n", "-f, --onredirect=<ok|warning|critical|follow>");
  printf ("    %s\n", _("How to handle redirected pages."));
  printf (" %s\n", "-m, --pagesize=INTEGER<:INTEGER>");
  printf ("    %s\n", _("Minimum page size required (bytes) : Maximum page size required (bytes)"));

  printf (UT_WARN_CRIT);

  printf (UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

  printf (UT_VERBOSE);

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("This plugin will attempt to open an HTTP connection with the host."));
  printf (" %s\n", _("Successful connects return STATE_OK, refusals and timeouts return STATE_CRITICAL"));
  printf (" %s\n", _("other errors return STATE_UNKNOWN.  Successful connects, but incorrect response"));
  printf (" %s\n", _("messages from the host result in STATE_WARNING return values.  If you are"));
  printf (" %s\n", _("checking a virtual server that uses 'host headers' you must supply the FQDN"));
  printf (" %s\n", _("(fully qualified domain name) as the [host_name] argument."));

#ifdef LIBCURL_FEATURE_SSL
  printf ("\n");
  printf (" %s\n", _("This plugin can also check whether an SSL enabled web server is able to"));
  printf (" %s\n", _("serve content (optionally within a specified time) or whether the X509 "));
  printf (" %s\n", _("certificate is still valid for the specified number of days."));
  printf ("\n");
  printf (" %s\n", _("Please note that this plugin does not check if the presented server"));
  printf (" %s\n", _("certificate matches the hostname of the server, or if the certificate"));
  printf (" %s\n", _("has a valid chain of trust to one of the locally installed CAs."));
  printf ("\n");
  printf ("%s\n", _("Examples:"));
  printf (" %s\n\n", "CHECK CONTENT: check_curl -w 5 -c 10 --ssl -H www.verisign.com");
  printf (" %s\n", _("When the 'www.verisign.com' server returns its content within 5 seconds,"));
  printf (" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
  printf (" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
  printf (" %s\n", _("a STATE_CRITICAL will be returned."));
  printf ("\n");
  printf (" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 14");
  printf (" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 14 days,"));
  printf (" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
  printf (" %s\n", _("14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when"));
  printf (" %s\n\n", _("the certificate is expired."));
  printf ("\n");
  printf (" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 30,14");
  printf (" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 30 days,"));
  printf (" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
  printf (" %s\n", _("30 days, but more than 14 days, a STATE_WARNING is returned."));
  printf (" %s\n", _("A STATE_CRITICAL will be returned when certificate expires in less than 14 days"));

  printf (" %s\n\n", "CHECK SSL WEBSERVER CONTENT VIA PROXY USING HTTP 1.1 CONNECT: ");
  printf (" %s\n", _("check_curl -I 192.168.100.35 -p 80 -u https://www.verisign.com/ -S -j CONNECT -H www.verisign.com "));
  printf (" %s\n", _("all these options are needed: -I <proxy> -p <proxy-port> -u <check-url> -S(sl) -j CONNECT -H <webserver>"));
  printf (" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
  printf (" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
  printf (" %s\n", _("a STATE_CRITICAL will be returned."));

#endif

  printf (UT_SUPPORT);

}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s -H <vhost> | -I <IP-address> [-u <uri>] [-p <port>]\n",progname);
  printf ("       [-J <client certificate file>] [-K <private key>] [--ca-cert <CA certificate file>]\n");
  printf ("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L] [-E] [-a auth]\n");
  printf ("       [-f <ok|warning|critcal|follow>]\n");
  printf ("       [-e <expect>] [-d string] [-s string] [-l] [-r <regex> | -R <case-insensitive regex>]\n");
  printf ("       [-P string] [-m <min_pg_size>:<max_pg_size>] [-4|-6] [-N] [-M <age>]\n");
  printf ("       [-A string] [-k string] [-S <version>] [--sni] [-C <warn_age>[,<crit_age>]]\n");
  printf ("       [-T <content-type>] [-j method]\n", progname);
  printf ("\n");
  printf ("%s\n", _("WARNING: check_curl is experimental. Please use"));
  printf ("%s\n\n", _("check_http if you need a stable version."));
}

void
print_curl_version (void)
{
  printf( "%s\n", curl_version());
}

int
curlhelp_initwritebuffer (curlhelp_write_curlbuf *buf)
{
  buf->bufsize = DEFAULT_BUFFER_SIZE;
  buf->buflen = 0;
  buf->buf = (char *)malloc ((size_t)buf->bufsize);
  if (buf->buf == NULL) return -1;
  return 0;
}

int
curlhelp_buffer_write_callback (void *buffer, size_t size, size_t nmemb, void *stream)
{
  curlhelp_write_curlbuf *buf = (curlhelp_write_curlbuf *)stream;

  while (buf->bufsize < buf->buflen + size * nmemb + 1) {
    buf->bufsize *= buf->bufsize * 2;
    buf->buf = (char *)realloc (buf->buf, buf->bufsize);
    if (buf->buf == NULL) return -1;
  }

  memcpy (buf->buf + buf->buflen, buffer, size * nmemb);
  buf->buflen += size * nmemb;
  buf->buf[buf->buflen] = '\0';

  return (int)(size * nmemb);
}

int
curlhelp_buffer_read_callback (void *buffer, size_t size, size_t nmemb, void *stream)
{
  curlhelp_read_curlbuf *buf = (curlhelp_read_curlbuf *)stream;
  
  size_t n = min (nmemb * size, buf->buflen - buf->pos);

  memcpy (buffer, buf->buf + buf->pos, n);
  buf->pos += n;
  
  return (int)n;  
}

void
curlhelp_freewritebuffer (curlhelp_write_curlbuf *buf)
{
  free (buf->buf);
  buf->buf = NULL;
}

int
curlhelp_initreadbuffer (curlhelp_read_curlbuf *buf, const char *data, size_t datalen)
{
  buf->buflen = datalen;
  buf->buf = (char *)malloc ((size_t)buf->buflen);
  if (buf->buf == NULL) return -1;
  memcpy (buf->buf, data, datalen);
  buf->pos = 0;
  return 0;
}

void
curlhelp_freereadbuffer (curlhelp_read_curlbuf *buf)
{
  free (buf->buf);
  buf->buf = NULL;
}

/* TODO: should be moved into 'gl' and should be probed, glibc has
 * a strrstr
 */
const char*
strrstr2(const char *haystack, const char *needle)
{
  int counter;
  size_t len;
  const char *prev_pos;
  const char *pos;

  if (haystack == NULL || needle == NULL)
    return NULL;

  if (haystack[0] == '\0' || needle[0] == '\0')
    return NULL;

  counter = 0;
  prev_pos = NULL;
  pos = haystack;
  len = strlen (needle);
  for (;;) {
    pos = strstr (pos, needle);
    if (pos == NULL) {
      if (counter == 0)
        return NULL;
      else
        return prev_pos;
    }
    counter++;
    prev_pos = pos;
    pos += len;
    if (*pos == '\0') return prev_pos;
  }
}

int
curlhelp_parse_statusline (const char *buf, curlhelp_statusline *status_line)
{
  char *first_line_end;
  char *p;
  size_t first_line_len;
  char *pp;
  const char *start;
  char *first_line_buf;

  /* find last start of a new header */
  start = strrstr2 (buf, "\r\nHTTP");
  if (start != NULL) {
    start += 2;
    buf = start;
  }

  first_line_end = strstr(buf, "\r\n");
  if (first_line_end == NULL) return -1;

  first_line_len = (size_t)(first_line_end - buf);
  status_line->first_line = (char *)malloc (first_line_len + 1);
  if (status_line->first_line == NULL) return -1;
  memcpy (status_line->first_line, buf, first_line_len);
  status_line->first_line[first_line_len] = '\0';
  first_line_buf = strdup( status_line->first_line );

  /* protocol and version: "HTTP/x.x" SP */

  p = strtok(first_line_buf, "/");
  if( p == NULL ) { free( first_line_buf ); return -1; }
  if( strcmp( p, "HTTP" ) != 0 ) { free( first_line_buf ); return -1; }

  p = strtok( NULL, "." );
  if( p == NULL ) { free( first_line_buf ); return -1; }
  status_line->http_major = (int)strtol( p, &pp, 10 );
  if( *pp != '\0' ) { free( first_line_buf ); return -1; }

  p = strtok( NULL, " " );
  if( p == NULL ) { free( first_line_buf ); return -1; }
  status_line->http_minor = (int)strtol( p, &pp, 10 );
  if( *pp != '\0' ) { free( first_line_buf ); return -1; }

  /* status code: "404" or "404.1", then SP */

  p = strtok( NULL, " ." );
  if( p == NULL ) { free( first_line_buf ); return -1; }
  if( strchr( p, '.' ) != NULL ) {
    char *ppp;
    ppp = strtok( p, "." );
    status_line->http_code = (int)strtol( ppp, &pp, 10 );
    if( *pp != '\0' ) { free( first_line_buf ); return -1; }

    ppp = strtok( NULL, "" );
    status_line->http_subcode = (int)strtol( ppp, &pp, 10 );
    if( *pp != '\0' ) { free( first_line_buf ); return -1; }
  } else {
    status_line->http_code = (int)strtol( p, &pp, 10 );
    status_line->http_subcode = -1;
    if( *pp != '\0' ) { free( first_line_buf ); return -1; }
  }

  /* Human readable message: "Not Found" CRLF */

  free( first_line_buf );
  p = strtok( NULL, "" );
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  status_line->msg = status_line->first_line + ( p - first_line_buf );

  return 0;
}

void
curlhelp_free_statusline (curlhelp_statusline *status_line)
{
  free (status_line->first_line);
}

void
remove_newlines (char *s)
{
  char *p;

  for (p = s; *p != '\0'; p++)
    if (*p == '\r' || *p == '\n')
      *p = ' ';
}

char *
perfd_time_ssl (double elapsed_time_ssl)
{
  return fperfdata ("time_ssl", elapsed_time_ssl, "s", FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0);
}

char *
get_header_value (const struct phr_header* headers, const size_t nof_headers, const char* header)
{
  int i;
  for( i = 0; i < nof_headers; i++ ) {
    if( strncasecmp( header, headers[i].name, max( headers[i].name_len, 4 ) ) == 0 ) {
      return strndup( headers[i].value, headers[i].value_len );
    }
  }
  return NULL;
}

/* TODO: use CURL_EXTERN time_t curl_getdate(const char *p, const time_t *unused); here */
static time_t
parse_time_string (const char *string)
{
  struct tm tm;
  time_t t;
  memset (&tm, 0, sizeof(tm));

  /* Like this: Tue, 25 Dec 2001 02:59:03 GMT */

  if (isupper (string[0])  &&  /* Tue */
    islower (string[1])  &&
    islower (string[2])  &&
    ',' ==   string[3]   &&
    ' ' ==   string[4]   &&
    (isdigit(string[5]) || string[5] == ' ') &&   /* 25 */
    isdigit (string[6])  &&
    ' ' ==   string[7]   &&
    isupper (string[8])  &&  /* Dec */
    islower (string[9])  &&
    islower (string[10]) &&
    ' ' ==   string[11]  &&
    isdigit (string[12]) &&  /* 2001 */
    isdigit (string[13]) &&
    isdigit (string[14]) &&
    isdigit (string[15]) &&
    ' ' ==   string[16]  &&
    isdigit (string[17]) &&  /* 02: */
    isdigit (string[18]) &&
    ':' ==   string[19]  &&
    isdigit (string[20]) &&  /* 59: */
    isdigit (string[21]) &&
    ':' ==   string[22]  &&
    isdigit (string[23]) &&  /* 03 */
    isdigit (string[24]) &&
    ' ' ==   string[25]  &&
    'G' ==   string[26]  &&  /* GMT */
    'M' ==   string[27]  &&  /* GMT */
    'T' ==   string[28]) {

    tm.tm_sec  = 10 * (string[23]-'0') + (string[24]-'0');
    tm.tm_min  = 10 * (string[20]-'0') + (string[21]-'0');
    tm.tm_hour = 10 * (string[17]-'0') + (string[18]-'0');
    tm.tm_mday = 10 * (string[5] == ' ' ? 0 : string[5]-'0') + (string[6]-'0');
    tm.tm_mon = (!strncmp (string+8, "Jan", 3) ? 0 :
      !strncmp (string+8, "Feb", 3) ? 1 :
      !strncmp (string+8, "Mar", 3) ? 2 :
      !strncmp (string+8, "Apr", 3) ? 3 :
      !strncmp (string+8, "May", 3) ? 4 :
      !strncmp (string+8, "Jun", 3) ? 5 :
      !strncmp (string+8, "Jul", 3) ? 6 :
      !strncmp (string+8, "Aug", 3) ? 7 :
      !strncmp (string+8, "Sep", 3) ? 8 :
      !strncmp (string+8, "Oct", 3) ? 9 :
      !strncmp (string+8, "Nov", 3) ? 10 :
      !strncmp (string+8, "Dec", 3) ? 11 :
      -1);
    tm.tm_year = ((1000 * (string[12]-'0') +
      100 * (string[13]-'0') +
      10 * (string[14]-'0') +
      (string[15]-'0'))
      - 1900);

    tm.tm_isdst = 0;  /* GMT is never in DST, right? */

    if (tm.tm_mon < 0 || tm.tm_mday < 1 || tm.tm_mday > 31)
      return 0;

    /*
    This is actually wrong: we need to subtract the local timezone
    offset from GMT from this value.  But, that's ok in this usage,
    because we only comparing these two GMT dates against each other,
    so it doesn't matter what time zone we parse them in.
    */

    t = mktime (&tm);
    if (t == (time_t) -1) t = 0;

    if (verbose) {
      const char *s = string;
      while (*s && *s != '\r' && *s != '\n')
      fputc (*s++, stdout);
      printf (" ==> %lu\n", (unsigned long) t);
    }

    return t;

  } else {
    return 0;
  }
}

int 
check_document_dates (const curlhelp_write_curlbuf *header_buf, char (*msg)[DEFAULT_BUFFER_SIZE])
{
  char *server_date = NULL;
  char *document_date = NULL;
  int date_result = STATE_OK;
  curlhelp_statusline status_line;
  struct phr_header headers[255];
  size_t nof_headers = 255;
  size_t msglen;
    
  int res = phr_parse_response (header_buf->buf, header_buf->buflen,
    &status_line.http_minor, &status_line.http_code, &status_line.msg, &msglen,
    headers, &nof_headers, 0);
  
  server_date = get_header_value (headers, nof_headers, "date");
  document_date = get_header_value (headers, nof_headers, "last-modified");

  if (!server_date || !*server_date) {
    snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sServer date unknown, "), *msg);
    date_result = max_state_alt(STATE_UNKNOWN, date_result);
  } else if (!document_date || !*document_date) {
    snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sDocument modification date unknown, "), *msg);
    date_result = max_state_alt(STATE_CRITICAL, date_result);
  } else {
    time_t srv_data = parse_time_string (server_date);
    time_t doc_data = parse_time_string (document_date);
    if (srv_data <= 0) {
      snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sServer date \"%100s\" unparsable, "), *msg, server_date);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data <= 0) {
      snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sDocument date \"%100s\" unparsable, "), *msg, document_date);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data > srv_data + 30) {
      snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sDocument is %d seconds in the future, "), *msg, (int)doc_data - (int)srv_data);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data < srv_data - maximum_age) {
      int n = (srv_data - doc_data);
      if (n > (60 * 60 * 24 * 2)) {
        snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sLast modified %.1f days ago, "), *msg, ((float) n) / (60 * 60 * 24));
        date_result = max_state_alt(STATE_CRITICAL, date_result);
      } else {
        snprintf (*msg, DEFAULT_BUFFER_SIZE, _("%sLast modified %d:%02d:%02d ago, "), *msg, n / (60 * 60), (n / 60) % 60, n % 60);
        date_result = max_state_alt(STATE_CRITICAL, date_result);
      }
    }
  }
  
  if (server_date) free (server_date);
  if (document_date) free (document_date);

  return date_result;
}
