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

#include "common.h"
#include "utils.h"

#ifndef LIBCURL_PROTOCOL_HTTP
#error libcurl compiled without HTTP support, compiling check_curl plugin makes not much sense
#endif

#include "curl/curl.h"
#include "curl/easy.h"

#define DEFAULT_BUFFER_SIZE 2048
#define DEFAULT_SERVER_URL "/"
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
} curlhelp_curlbuf;

/* for parsing the HTTP status line */
typedef struct {
  int http_major;   /* major version of the protocol, always 1 (HTTP/0.9
                     * never reached the big internet most likely) */
  int http_minor;   /* minor version of the protocol, usually 0 or 1 */
  int http_code;    /* HTTP return code as in RFC 2145 */
  int http_subcode; /* Microsoft IIS extension, HTTP subcodes, see
                     * http://support.microsoft.com/kb/318380/en-us */
  char *msg;        /* the human readable message */
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
char output_string_search[30] = "";
char *warning_thresholds = NULL;
char *critical_thresholds = NULL;
int days_till_exp_warn, days_till_exp_crit;
thresholds *thlds;
char user_agent[DEFAULT_BUFFER_SIZE];
int verbose = 0;
char *http_method = NULL;
CURL *curl;
struct curl_slist *header_list = NULL;
curlhelp_curlbuf body_buf;
curlhelp_curlbuf header_buf;
curlhelp_statusline status_line;
char http_header[DEFAULT_BUFFER_SIZE];
struct curl_slist *http_opt_headers = NULL;
long code;
long socket_timeout = DEFAULT_SOCKET_TIMEOUT;
double total_time;
char errbuf[CURL_ERROR_SIZE+1];
CURLcode res;
char url[DEFAULT_BUFFER_SIZE];
char msg[DEFAULT_BUFFER_SIZE];
char perfstring[DEFAULT_BUFFER_SIZE];
char string_expect[MAX_INPUT_BUFFER] = "";
char user_auth[MAX_INPUT_BUFFER] = "";
int onredirect = STATE_OK;
int use_ssl = FALSE;
int use_sni = TRUE;
int check_cert = FALSE;
int ssl_version = CURL_SSLVERSION_DEFAULT;
char *client_cert = NULL;
char *client_privkey = NULL;
char *ca_cert = NULL;
X509 *cert = NULL;

int process_arguments (int, char**);
int check_http (void);
void print_help (void);
void print_usage (void);
void print_curl_version (void);
int curlhelp_initbuffer (curlhelp_curlbuf*);
int curlhelp_buffer_callback (void*, size_t , size_t , void*);
void curlhelp_freebuffer (curlhelp_curlbuf*);

int curlhelp_parse_statusline (const char*, curlhelp_statusline *);
void curlhelp_free_statusline (curlhelp_statusline *);

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

  result = check_http ();
  return result;
}

int verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx)
{
  cert = X509_STORE_CTX_get_current_cert(x509_ctx);
  return 1;
}

CURLcode sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm)
{
  SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, verify_callback);

  return CURLE_OK;
}

int
check_http (void)
{
  int result = STATE_OK;

  /* initialize curl */
  if (curl_global_init (CURL_GLOBAL_DEFAULT) != CURLE_OK)
    die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_global_init failed\n");

  if ((curl = curl_easy_init()) == NULL)
    die (STATE_UNKNOWN, "HTTP UNKNOWN - curl_easy_init failed\n");

  if (verbose >= 3)
    curl_easy_setopt (curl, CURLOPT_VERBOSE, TRUE);

  /* print everything on stdout like check_http would do */
  curl_easy_setopt(curl, CURLOPT_STDERR, stdout);

  /* initialize buffer for body of the answer */
  if (curlhelp_initbuffer(&body_buf) < 0)
    die (STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for body\n");
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)curlhelp_buffer_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, (void *)&body_buf);

  /* initialize buffer for header of the answer */
  if (curlhelp_initbuffer( &header_buf ) < 0)
    die (STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for header\n" );
  curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, (curl_write_callback)curlhelp_buffer_callback);
  curl_easy_setopt (curl, CURLOPT_WRITEHEADER, (void *)&header_buf);

  /* set the error buffer */
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, errbuf);

  /* set timeouts */
  curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, socket_timeout);
  curl_easy_setopt (curl, CURLOPT_TIMEOUT, socket_timeout);

  /* compose URL */
  snprintf (url, DEFAULT_BUFFER_SIZE, "%s://%s%s", use_ssl ? "https" : "http",
    server_address, server_url);
  curl_easy_setopt (curl, CURLOPT_URL, url);

  /* set port */
  curl_easy_setopt (curl, CURLOPT_PORT, server_port);

  /* set HTTP method */
  if (http_method) {
    if (!strcmp(http_method, "POST"))
      curl_easy_setopt (curl, CURLOPT_POST, 1);
    else if (!strcmp(http_method, "PUT"))
      curl_easy_setopt (curl, CURLOPT_PUT, 1);
    curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, http_method);
  }

  /* set hostname (virtual hosts) */
  snprintf (http_header, DEFAULT_BUFFER_SIZE, "Host: %s", host_name);
  header_list = curl_slist_append (header_list, http_header);

  /* always close connection, be nice to servers */
  snprintf (http_header, DEFAULT_BUFFER_SIZE, "Connection: close");
  header_list = curl_slist_append (header_list, http_header);

  /* set HTTP headers */
  curl_easy_setopt( curl, CURLOPT_HTTPHEADER, header_list );

  /* set SSL version, warn about unsecure or unsupported versions */
  if (use_ssl) {
    curl_easy_setopt (curl, CURLOPT_SSLVERSION, ssl_version);
  }

  /* client certificate and key to present to server (SSL) */
  if (client_cert)
    curl_easy_setopt (curl, CURLOPT_SSLCERT, client_cert);
  if (client_privkey)
    curl_easy_setopt (curl, CURLOPT_SSLKEY, client_privkey);
  if (ca_cert)
    curl_easy_setopt (curl, CURLOPT_CAINFO, ca_cert);

  /* per default if we have a CA verify both the peer and the
   * hostname in the certificate, can be switched off later */
  curl_easy_setopt( curl, CURLOPT_SSL_VERIFYPEER, 2);
  curl_easy_setopt( curl, CURLOPT_SSL_VERIFYHOST, 2);

  /* backward-compatible behaviour, be tolerant in checks
   * TODO: depending on more options have aspects we want
   * to be less tolerant about ssl verfications
   */
  curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0);
  curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0);

  /* set callback to extract certificate */
  if(check_cert) {
    curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun);
  }

  /* set default or user-given user agent identification */
  curl_easy_setopt (curl, CURLOPT_USERAGENT, user_agent);

  /* authentication */
  if (strcmp(user_auth, ""))
    curl_easy_setopt (curl, CURLOPT_USERPWD, user_auth);

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
   * curl_easy_setopt( curl, CURLOPT_HTTPAUTH, (long)CURLAUTH_DIGEST );
   */

  /* TODO: --cacert: CA certificate file to verify SSL connection against (SSL) */
  /* if( args_info.cacert_given ) {
      curl_easy_setopt( curl, CURLOPT_CAINFO, args_info.cacert_arg );
   } */

  /* handle redirections */
  if (onredirect == STATE_DEPENDENT) {
    curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);
    /* TODO: handle the following aspects of redirection
      CURLOPT_POSTREDIR: method switch
      CURLINFO_REDIRECT_URL: custom redirect option
      CURLOPT_REDIRECT_PROTOCOLS
      CURLINFO_REDIRECT_COUNT
    */
  }

  /* set optional http header */
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_opt_headers);

  /* do the request */
  res = curl_easy_perform(curl);

  /* free header list, we don't need it anymore */
  curl_slist_free_all(header_list);
  curl_slist_free_all(http_opt_headers);

  /* Curl errors, result in critical Nagios state */
  if (res != CURLE_OK) {
    remove_newlines (errbuf);
    snprintf (msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host on port %d: cURL returned %d - %s\n"),
      server_port, res, curl_easy_strerror(res));
    die (STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
  }

  /* certificate checks */
#ifdef HAVE_SSL
  if (use_ssl == TRUE) {
    if (check_cert == TRUE) {
      result = np_net_ssl_check_certificate(cert, days_till_exp_warn, days_till_exp_crit);
      return(result);
    }
  }
#endif /* HAVE_SSL */

  /* we got the data and we executed the request in a given time, so we can append
   * performance data to the answer always
   */
  curl_easy_getinfo (curl, CURLINFO_TOTAL_TIME, &total_time);
  snprintf (perfstring, DEFAULT_BUFFER_SIZE, "time=%.6gs;%.6g;%.6g;%.6g size=%dB;;;0",
    total_time,
    0.0, 0.0,
    ( warning_thresholds != NULL ) ? (double)thlds->warning->end : 0.0,
    critical_thresholds != NULL ? (double)thlds->critical->end : 0.0,
    0.0,
    (int)body_buf.buflen);

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
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &code);
  if (verbose>=2)
    printf ("* curl CURLINFO_RESPONSE_CODE is %d\n", code);

  /* print status line, header, body if verbose */
  if (verbose >= 2) {
    puts ("--- HEADER ---");
    puts (header_buf.buf);
    puts ("--- BODY ---");
    puts (body_buf.buf);
  }

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

  /* check status codes, set exit status accordingly */
  if( status_line.http_code != code ) {
    die (STATE_CRITICAL, _("HTTP CRITICAL HTTP/%d.%d %d %s - different HTTP codes (cUrl has %ld)\n"),
      status_line.http_major, status_line.http_minor,
      status_line.http_code, status_line.msg, code);
  }

  /* Page and Header content checks go here */
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
        snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spattern not found"), msg);
      else
        snprintf (msg, DEFAULT_BUFFER_SIZE, _("%spattern found"), msg);
      result = STATE_CRITICAL;
    }
    else {
      /* FIXME: Shouldn't that be UNKNOWN? */
      regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
      snprintf (msg, DEFAULT_BUFFER_SIZE, _("%sExecute Error: %s, "), msg, errbuf);
      result = STATE_CRITICAL;
    }
  }

  /* -w, -c: check warning and critical level */
  result = max_state_alt(get_status(total_time, thlds), result);

  /* TODO: separate _() msg and status code: die (result, "HTTP %s: %s\n", state_text(result), msg); */
  die (result, "HTTP %s HTTP/%d.%d %d %s - %s - %.3g seconds response time|%s\n",
    state_text(result), status_line.http_major, status_line.http_minor,
    status_line.http_code, status_line.msg, msg,
    total_time, perfstring);

  /* proper cleanup after die? */
  curlhelp_free_statusline(&status_line);
  curl_easy_cleanup (curl);
  curl_global_cleanup ();
  curlhelp_freebuffer(&body_buf);
  curlhelp_freebuffer(&header_buf);

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
  int c = 1;
  char *temp;

  enum {
    INVERT_REGEX = CHAR_MAX + 1,
    SNI_OPTION,
    CA_CERT_OPTION
  };

  int option = 0;
  static struct option longopts[] = {
    STD_LONG_OPTS,
    {"ssl", optional_argument, 0, 'S'},
    {"sni", no_argument, 0, SNI_OPTION},
    {"method", required_argument, 0, 'j'},
    {"IP-address", required_argument, 0, 'I'},
    {"url", required_argument, 0, 'u'},
    {"port", required_argument, 0, 'p'},
    {"authorization", required_argument, 0, 'a'},
    {"string", required_argument, 0, 's'},
    {"regex", required_argument, 0, 'r'},
    {"onredirect", required_argument, 0, 'f'},
    {"certificate", required_argument, 0, 'C'},
    {"client-cert", required_argument, 0, 'J'},
    {"private-key", required_argument, 0, 'K'},
    {"ca-cert", required_argument, 0, CA_CERT_OPTION},
    {"useragent", required_argument, 0, 'A'},
    {"invert-regex", no_argument, NULL, INVERT_REGEX},
    {"header", required_argument, 0, 'k'},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    return ERROR;

  while (1) {
    c = getopt_long (argc, argv, "Vvht:c:w:A:k:H:j:I:a:p:s:r:u:f:C:J:K:S::", longopts, &option);
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
    case 'j': /* Set HTTP method */
      if (http_method)
        free(http_method);
      http_method = strdup (optarg);
      break;
    case 'A': /* useragent */
      snprintf (user_agent, DEFAULT_BUFFER_SIZE, optarg);
      break;
    case 'k': /* Additional headers */
      http_opt_headers = curl_slist_append(http_opt_headers, optarg);
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
      /* ssl_version initialized to CURL_SSLVERSION_TLSv1_0 as a default. Only set if it's non-zero.  This helps when we include multiple
         parameters, like -S and -C combinations */
      ssl_version = CURL_SSLVERSION_TLSv1_0;
      if (c=='S' && optarg != NULL) {
        int got_plus = strchr(optarg, '+') != NULL;

        if (!strncmp (optarg, "1.2", 3))
          ssl_version = CURL_SSLVERSION_TLSv1_2;
        else if (!strncmp (optarg, "1.1", 3))
          ssl_version = CURL_SSLVERSION_TLSv1_1;
        else if (optarg[0] == '1')
          ssl_version = CURL_SSLVERSION_TLSv1_0;
        else if (optarg[0] == '3')
          ssl_version = CURL_SSLVERSION_SSLv3;
        else if (optarg[0] == '2')
          ssl_version = CURL_SSLVERSION_SSLv2;
        else
          usage4 (_("Invalid option - Valid SSL/TLS versions: 2, 3, 1, 1.1, 1.2 (with optional '+' suffix)"));
      }
      if (server_port == HTTP_PORT)
        server_port = HTTPS_PORT;
#else
      /* -C -J and -K fall through to here without SSL */
      usage4 (_("Invalid option - SSL is not available"));
#endif
      break;
    case SNI_OPTION: /* --sni is parsed, but ignored, the default is TRUE with libcurl */
      use_sni = TRUE;
      break;
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
    case 's': /* string or substring */
      strncpy (string_expect, optarg, MAX_INPUT_BUFFER - 1);
      string_expect[MAX_INPUT_BUFFER - 1] = 0;
      break;
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

  //~ if (virtual_port == 0)
    //~ virtual_port = server_port;

  return TRUE;
}

void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf ("Copyright (c) 2017 Andreas Baumann <abaumann@yahoo.com>\n");
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
  printf (" %s\n", "-C, --certificate");
  printf ("    %s\n", _("Check validity of certificate"));
  printf (" %s\n", "-J, --client-cert=FILE");
  printf ("   %s\n", _("Name of file that contains the client certificate (PEM format)"));
  printf ("   %s\n", _("to be used in establishing the SSL session"));
  printf (" %s\n", "-K, --private-key=FILE");
  printf ("   %s\n", _("Name of file containing the private key (PEM format)"));
  printf ("   %s\n", _("matching the client certificate"));
  printf (" %s\n", "--ca-cert=FILE");
  printf ("   %s\n", _("CA certificate file to verify peer against"));
#endif

  printf (" %s\n", "-s, --string=STRING");
  printf ("    %s\n", _("String to expect in the content"));
  printf (" %s\n", "-u, --url=PATH");
  printf ("    %s\n", _("URL to GET or POST (default: /)"));
  printf (" %s\n", "-j, --method=STRING  (for example: HEAD, OPTIONS, TRACE, PUT, DELETE, CONNECT)");
  printf ("    %s\n", _("Set HTTP method."));
  printf (" %s\n", "-r, --regex, --ereg=STRING");
  printf ("    %s\n", _("Search page for regex STRING"));
  printf (" %s\n", "-a, --authorization=AUTH_PAIR");
  printf ("    %s\n", _("Username:password on sites with basic authentication"));
  printf (" %s\n", "-A, --useragent=STRING");
  printf ("    %s\n", _("String to be sent in http header as \"User Agent\""));
  printf (" %s\n", "-f, --onredirect=<ok|warning|critical|follow>");
  printf ("    %s\n", _("How to handle redirected pages."));

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
  printf ("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-a auth]\n");
  printf ("       [-f <ok|warning|critcal|follow>]\n");
  printf ("       [-s string] [-r <regex>\n");
  printf ("       [-A string] [-S <version>] [-C]\n");
  printf ("       [-v verbose]\n", progname);
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
curlhelp_initbuffer (curlhelp_curlbuf *buf)
{
  buf->bufsize = DEFAULT_BUFFER_SIZE;
  buf->buflen = 0;
  buf->buf = (char *)malloc ((size_t)buf->bufsize);
  if (buf->buf == NULL) return -1;
  return 0;
}

int
curlhelp_buffer_callback (void *buffer, size_t size, size_t nmemb, void *stream)
{
  curlhelp_curlbuf *buf = (curlhelp_curlbuf *)stream;

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

void
curlhelp_freebuffer (curlhelp_curlbuf *buf)
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

  /* protocol and version: "HTTP/x.x" SP */

  p = strtok(status_line->first_line, "/");
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  if( strcmp( p, "HTTP" ) != 0 ) { free( status_line->first_line ); return -1; }

  p = strtok( NULL, "." );
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  status_line->http_major = (int)strtol( p, &pp, 10 );
  if( *pp != '\0' ) { free( status_line->first_line ); return -1; }

  p = strtok( NULL, " " );
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  status_line->http_minor = (int)strtol( p, &pp, 10 );
  if( *pp != '\0' ) { free( status_line->first_line ); return -1; }

  /* status code: "404" or "404.1", then SP */

  p = strtok( NULL, " ." );
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  if( strchr( p, '.' ) != NULL ) {
    char *ppp;
    ppp = strtok( p, "." );
    status_line->http_code = (int)strtol( ppp, &pp, 10 );
    if( *pp != '\0' ) { free( status_line->first_line ); return -1; }

    ppp = strtok( NULL, "" );
    status_line->http_subcode = (int)strtol( ppp, &pp, 10 );
    if( *pp != '\0' ) { free( status_line->first_line ); return -1; }
  } else {
    status_line->http_code = (int)strtol( p, &pp, 10 );
    status_line->http_subcode = -1;
    if( *pp != '\0' ) { free( status_line->first_line ); return -1; }
  }

  /* Human readable message: "Not Found" CRLF */

  p = strtok( NULL, "" );
  if( p == NULL ) { free( status_line->first_line ); return -1; }
  status_line->msg = p;

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
