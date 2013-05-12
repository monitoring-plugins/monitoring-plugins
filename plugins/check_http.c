/*****************************************************************************
* 
* Nagios check_http plugin
* 
* License: GPL
* Copyright (c) 1999-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_http plugin
* 
* This plugin tests the HTTP service on the specified host. It can test
* normal (http) and secure (https) servers, follow redirects, search for
* strings and regular expressions, check connection times, and report on
* certificate expiration times.
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
* 
*****************************************************************************/

/* splint -I. -I../../plugins -I../../lib/ -I/usr/kerberos/include/ ../../plugins/check_http.c */

const char *progname = "check_http";
const char *copyright = "1999-2011";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "base64.h"
#include <ctype.h>

#define INPUT_DELIMITER ";"
#define STICKY_NONE 0
#define STICKY_HOST 1
#define STICKY_PORT 2

#define HTTP_EXPECT "HTTP/1."
enum {
  MAX_IPV4_HOSTLENGTH = 255,
  HTTP_PORT = 80,
  HTTPS_PORT = 443,
  MAX_PORT = 65535
};

#ifdef HAVE_SSL
int check_cert = FALSE;
int ssl_version;
int days_till_exp_warn, days_till_exp_crit;
char *randbuff;
X509 *server_cert;
#  define my_recv(buf, len) ((use_ssl) ? np_net_ssl_read(buf, len) : read(sd, buf, len))
#  define my_send(buf, len) ((use_ssl) ? np_net_ssl_write(buf, len) : send(sd, buf, len, 0))
#else /* ifndef HAVE_SSL */
#  define my_recv(buf, len) read(sd, buf, len)
#  define my_send(buf, len) send(sd, buf, len, 0)
#endif /* HAVE_SSL */
int no_body = FALSE;
int maximum_age = -1;

enum {
  REGS = 2,
  MAX_RE_SIZE = 256
};
#include "regex.h"
regex_t preg;
regmatch_t pmatch[REGS];
char regexp[MAX_RE_SIZE];
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
int errcode;
int invert_regex = 0;

struct timeval tv;

#define HTTP_URL "/"
#define CRLF "\r\n"

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
char output_string_search[30] = "";
char *warning_thresholds = NULL;
char *critical_thresholds = NULL;
thresholds *thlds;
char user_auth[MAX_INPUT_BUFFER] = "";
char proxy_auth[MAX_INPUT_BUFFER] = "";
int display_html = FALSE;
char **http_opt_headers;
int http_opt_headers_count = 0;
int onredirect = STATE_OK;
int followsticky = STICKY_NONE;
int use_ssl = FALSE;
int use_sni = FALSE;
int verbose = FALSE;
int sd;
int min_page_len = 0;
int max_page_len = 0;
int redir_depth = 0;
int max_depth = 15;
char *http_method;
char *http_post_data;
char *http_content_type;
char buffer[MAX_INPUT_BUFFER];

int process_arguments (int, char **);
int check_http (void);
void redir (char *pos, char *status_line);
int server_type_check(const char *type);
int server_port_check(int ssl_flag);
char *perfd_time (double microsec);
char *perfd_size (int page_len);
void print_help (void);
void print_usage (void);

static struct help_head resource_meta = {
	"http",
	"Test the HTTP service",
	"This plugin tests the HTTP service on the specified host. It can test\n"
	"normal (http) and secure (https) servers, follow redirects, search for\n"
	"strings and regular expressions, check connection times, and report on\n"
	"certificate expiration times.\n"
};

static struct parameter_help options_help[] = {
	/* hostname */
	{
		"hostname", 'H',
		"Host name",
		0, 1, "string", "", "ADDRESS",
		"Host name argument for servers using host headers (virtual host)\n"
		"Append a port to include it in the header (eg: example.com:5000)\n"
	},
	/* IP-address */
	{
		"IP-address", 'I',
		"IP address or name",
		0, 0, "string", "", "ADDRESS",
		"IP address or name (use numeric address if possible to bypass DNS lookup).\n"
	},
	/* port */
	{
		"port", 'p',
		"Port number (default: 80)",
		0, 0, "integer", "80", "INTEGER",
	},
	/* use-ipv4 */
	{
		"use-ipv4", '4',
		"Use IPv4 connection",
		0, 0, "boolean", "false", "",
	},
	/* use-ipv6 */
	{
		"use-ipv6", '6',
		"Use IPv6 connection",
		0, 0, "boolean", "false", "",
	},
#ifdef HAVE_SSL
	/* ssl */
	{
		"ssl", 'S',
		"Connect via SSL.",
		0, 0, "string", "", "VERSION",
		"Connect via SSL. Port defaults to 443. VERSION is optional, and prevents\n"
		"auto-negotiation (1 = TLSv1, 2 = SSLv2, 3 = SSLv3).\n"
	},
	/* sni */
	{
		"sni", 0,
		"Enable SSL/TLS hostname extension support (SNI)",
		0, 0, "boolean", "false", "",
	},
	/* certificate */
	{
		"certificate", 'C',
		"Minimum number of days a certificate has to be valid",
		0, 0, "integer", "", "INTEGER",
		"Minimum number of days a certificate has to be valid. Port defaults to 443\n"
		"(when this option is used the URL is not checked.)\n"
	},
#endif
	/* expect */
	{
		"expect", 'e',
		"list of strings of which at least one must match",
		0, 0, "string", "", "STRING",
		"Comma-delimited list of strings, at least one of them is expected in\n"
		"the first (status) line of the server response (default: HTTP/1.)\n"
		"If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)\n"
	},
	/* string */
	{
		"string", 's',
		"String to expect in the content",
		0, 0, "string", "", "STRING",
	},
	/* url */
	{
		"url", 'u',
		"URL to GET or POST (default: /)",
		0, 0, "string", "/", "PATH",
	},
	/* post */
	{
		"post", 'P',
		"URL encoded http POST data",
		0, 0, "string", "", "STRING",
	},
	/* method */
	{
		"method", 'j',
		"Set HTTP method.",
		0, 0, "string", "", "STRING",
	},
	/* no-body */
	{
		"no-body", 'N',
		"Don't wait for document body",
		0, 0, "boolean", "false", "",
		"Don't wait for document body: stop reading after headers.\n"
		"(Note that this still does an HTTP GET or POST, not a HEAD.)\n"
	},
	/* max-age */
	{
		"max-age", 'M',
		"maximum age of document",
		0, 0, "string", "", "SECONDS",
		"Warn if document is more than SECONDS old. the number can also be of\n"
		"the form \"10m\" for minutes, \"10h\" for hours, or \"10d\" for days.\n"
	},
	/* content-type */
	{
		"content-type", 'T',
		"specify Content-Type header media type when POSTing",
		0, 0, "string", "", "STRING",
	},
	/* linespan */
	{
		"linespan", 'l',
		"Allow regex to span newlines (must precede -r or -R)",
		0, 0, "boolean", "false", "",
	},
	/* regex */
	{
		"regex", 'r',
		"Search page for regex STRING",
		0, 0, "boolean", "false", "",
	},
	/* eregi */
	{
		"eregi", 'R',
		"Search page for case-insensitive regex STRING",
		0, 0, "string", "", "STRING",
	},
	/* invert-regex */
	{
		"invert-regex", 0,
		"Return CRITICAL if found, OK if not",
		0, 0, "boolean", "false", "",
	},
	/* authorization */
	{
		"authorization", 'a',
		"Username:password on sites with basic authentication",
		0, 0, "string", "", "AUTH_PAIR",
	},
	/* proxy_authorization */
	{
		"proxy_authorization", 'b',
		"Username:password on proxy-servers with basic authentication",
		0, 0, "string", "", "AUTH_PAIR",
	},
	/* useragent */
	{
		"useragent", 'A',
		"String to be sent in http header as \"User Agent\"",
		0, 0, "string", "", "STRING",
	},
	/* header */
	{
		"header", 'k',
		"other tags to be sent in http header",
		0, 0, "string", "", "STRING",
		"Any other tags to be sent in http header. Use multiple times for additional headers\n"
	},
	/* link */
	{
		"link", 'L',
		"Wrap output in HTML link (obsoleted by urlize)",
		0, 0, "boolean", "false", "",
	},
	/* onredirect */
	{
		"onredirect", 'f',
		"How to handle redirected pages",
		0, 0, "string", "", "<ok|warning|critical|follow|sticky|stickyport>",
		"How to handle redirected pages. sticky is like follow but stick to the\n"
		"specified IP address. stickyport also ensures port stays the same.\n"
	},
	/* pagesize */
	{
		"pagesize", 'm',
		"minimum[:maximum] page size",
		0, 0, "string", "", "INTEGER<:INTEGER>",
		"Minimum page size required (bytes) : Maximum page size required (bytes)\n"
	},
	/* warning */
	{
		"warning", 'w',
		"Response time to result in warning status (seconds)",
		0, 0, "string", "", "DOUBLE",
	},
	/* critical */
	{
		"critical", 'c',
		"Response time to result in critical status (seconds)",
		0, 0, "string", "", "DOUBLE",
	},
	/* timeout */
	{
		"timeout", 't',
		"Seconds before connection times out (default: 10)",
		0, 0, "integer", "10", "INTEGER",
	},
	/* extra-opts */
	{
		"extra-opts", 0,
		"ini file with extra options",
		0, 0, "string", "", "string",
		"Read options from an ini file. See http://nagiosplugins.org/extra-opts\n"
		"for usage and examples.\n"
	},
	{}
};

int
main (int argc, char **argv)
{
  int result = STATE_UNKNOWN;

  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  /* Set default URL. Must be malloced for subsequent realloc if --onredirect=follow */
  server_url = strdup(HTTP_URL);
  server_url_length = strlen(server_url);
  xasprintf (&user_agent, "User-Agent: check_http/v%s (nagios-plugins %s)",
            NP_VERSION, VERSION);

  /* Parse extra opts if any */
  if (argc==2 && !strcmp(argv[1], "--metadata")) {
    /* dump metadata and exit */
    print_meta_data(&resource_meta, options_help);
    exit(0);
  }

  argv=np_extra_opts (&argc, argv, progname);

  if (process_arguments (argc, argv) == ERROR)
    usage4 (_("Could not parse arguments"));

  if (display_html == TRUE)
    printf ("<A HREF=\"%s://%s:%d%s\" target=\"_blank\">",
      use_ssl ? "https" : "http", host_name ? host_name : server_address,
      server_port, server_url);

  /* initialize alarm signal handling, set socket timeout, start timer */
  (void) signal (SIGALRM, socket_timeout_alarm_handler);
  (void) alarm (socket_timeout);
  gettimeofday (&tv, NULL);

  result = check_http ();
  return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
  int c = 1;
  char *p;
  char *temp;

  enum {
    INVERT_REGEX = CHAR_MAX + 1,
    SNI_OPTION
  };

  int option = 0;
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
    {"proxy_authorization", required_argument, 0, 'b'},
    {"string", required_argument, 0, 's'},
    {"expect", required_argument, 0, 'e'},
    {"regex", required_argument, 0, 'r'},
    {"ereg", required_argument, 0, 'r'},
    {"eregi", required_argument, 0, 'R'},
    {"linespan", no_argument, 0, 'l'},
    {"onredirect", required_argument, 0, 'f'},
    {"certificate", required_argument, 0, 'C'},
    {"useragent", required_argument, 0, 'A'},
    {"header", required_argument, 0, 'k'},
    {"no-body", no_argument, 0, 'N'},
    {"max-age", required_argument, 0, 'M'},
    {"content-type", required_argument, 0, 'T'},
    {"pagesize", required_argument, 0, 'm'},
    {"invert-regex", no_argument, NULL, INVERT_REGEX},
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
    c = getopt_long (argc, argv, "Vvh46t:c:w:A:k:H:P:j:T:I:a:b:e:p:s:R:r:u:f:C:nlLS::m:M:N", longopts, &option);
    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case '?': /* usage */
      usage5 ();
      break;
    case 'h': /* help */
      print_help ();
      exit (STATE_OK);
      break;
    case 'V': /* version */
      print_revision (progname, NP_VERSION);
      exit (STATE_OK);
      break;
    case 't': /* timeout period */
      if (!is_intnonneg (optarg))
        usage2 (_("Timeout interval must be a positive integer"), optarg);
      else
        socket_timeout = atoi (optarg);
      break;
    case 'c': /* critical time threshold */
      critical_thresholds = optarg;
      break;
    case 'w': /* warning time threshold */
      warning_thresholds = optarg;
      break;
    case 'A': /* User Agent String */
      xasprintf (&user_agent, "User-Agent: %s", optarg);
      break;
    case 'k': /* Additional headers */
      if (http_opt_headers_count == 0)
        http_opt_headers = malloc (sizeof (char *) * (++http_opt_headers_count));
      else
        http_opt_headers = realloc (http_opt_headers, sizeof (char *) * (++http_opt_headers_count));
      http_opt_headers[http_opt_headers_count - 1] = optarg;
      /* xasprintf (&http_opt_headers, "%s", optarg); */
      break;
    case 'L': /* show html link */
      display_html = TRUE;
      break;
    case 'n': /* do not show html link */
      display_html = FALSE;
      break;
    case 'C': /* Check SSL cert validity */
#ifdef HAVE_SSL
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
      /* Fall through to -S option */
#endif
    case 'S': /* use SSL */
#ifdef HAVE_SSL
      use_ssl = TRUE;
      if (optarg == NULL || c != 'S')
        ssl_version = 0;
      else {
        ssl_version = atoi(optarg);
        if (ssl_version < 1 || ssl_version > 3)
            usage4 (_("Invalid option - Valid values for SSL Version are 1 (TLSv1), 2 (SSLv2) or 3 (SSLv3)"));
      }
      if (specify_port == FALSE)
        server_port = HTTPS_PORT;
#else
      usage4 (_("Invalid option - SSL is not available"));
#endif
      break;
    case SNI_OPTION:
      use_sni = TRUE;
      break;
    case 'f': /* onredirect */
      if (!strcmp (optarg, "stickyport"))
        onredirect = STATE_DEPENDENT, followsticky = STICKY_HOST|STICKY_PORT;
      else if (!strcmp (optarg, "sticky"))
        onredirect = STATE_DEPENDENT, followsticky = STICKY_HOST;
      else if (!strcmp (optarg, "follow"))
        onredirect = STATE_DEPENDENT, followsticky = STICKY_NONE;
      else if (!strcmp (optarg, "unknown"))
        onredirect = STATE_UNKNOWN;
      else if (!strcmp (optarg, "ok"))
        onredirect = STATE_OK;
      else if (!strcmp (optarg, "warning"))
        onredirect = STATE_WARNING;
      else if (!strcmp (optarg, "critical"))
        onredirect = STATE_CRITICAL;
      else usage2 (_("Invalid onredirect option"), optarg);
      if (verbose)
        printf(_("option f:%d \n"), onredirect);
      break;
    /* Note: H, I, and u must be malloc'd or will fail on redirects */
    case 'H': /* Host Name (virtual host) */
      host_name = strdup (optarg);
      if (host_name[0] == '[') {
        if ((p = strstr (host_name, "]:")) != NULL) /* [IPv6]:port */
          server_port = atoi (p + 2);
      } else if ((p = strchr (host_name, ':')) != NULL
                 && strchr (++p, ':') == NULL) /* IPv4:port or host:port */
          server_port = atoi (p);
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
        usage2 (_("Invalid port number"), optarg);
      else {
        server_port = atoi (optarg);
        specify_port = TRUE;
      }
      break;
    case 'a': /* authorization info */
      strncpy (user_auth, optarg, MAX_INPUT_BUFFER - 1);
      user_auth[MAX_INPUT_BUFFER - 1] = 0;
      break;
    case 'b': /* proxy-authorization info */
      strncpy (proxy_auth, optarg, MAX_INPUT_BUFFER - 1);
      proxy_auth[MAX_INPUT_BUFFER - 1] = 0;
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
      xasprintf (&http_content_type, "%s", optarg);
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
#ifdef USE_IPV6
      address_family = AF_INET6;
#else
      usage4 (_("IPv6 support not available"));
#endif
      break;
    case 'v': /* verbose */
      verbose = TRUE;
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
                  }
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

  if (http_method == NULL)
    http_method = strdup ("GET");

  return TRUE;
}



/* Returns 1 if we're done processing the document body; 0 to keep going */
static int
document_headers_done (char *full_page)
{
  const char *body;

  for (body = full_page; *body; body++) {
    if (!strncmp (body, "\n\n", 2) || !strncmp (body, "\n\r\n", 3))
      break;
  }

  if (!*body)
    return 0;  /* haven't read end of headers yet */

  full_page[body - full_page] = 0;
  return 1;
}

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

static int
check_document_dates (const char *headers, char **msg)
{
  const char *s;
  char *server_date = 0;
  char *document_date = 0;
  int date_result = STATE_OK;

  s = headers;
  while (*s) {
    const char *field = s;
    const char *value = 0;

    /* Find the end of the header field */
    while (*s && !isspace(*s) && *s != ':')
      s++;

    /* Remember the header value, if any. */
    if (*s == ':')
      value = ++s;

    /* Skip to the end of the header, including continuation lines. */
    while (*s && !(*s == '\n' && (s[1] != ' ' && s[1] != '\t')))
      s++;

    /* Avoid stepping over end-of-string marker */
    if (*s)
      s++;

    /* Process this header. */
    if (value && value > field+2) {
      char *ff = (char *) malloc (value-field);
      char *ss = ff;
      while (field < value-1)
        *ss++ = tolower(*field++);
      *ss++ = 0;

      if (!strcmp (ff, "date") || !strcmp (ff, "last-modified")) {
        const char *e;
        while (*value && isspace (*value))
          value++;
        for (e = value; *e && *e != '\r' && *e != '\n'; e++)
          ;
        ss = (char *) malloc (e - value + 1);
        strncpy (ss, value, e - value);
        ss[e - value] = 0;
        if (!strcmp (ff, "date")) {
          if (server_date) free (server_date);
          server_date = ss;
        } else {
          if (document_date) free (document_date);
          document_date = ss;
        }
      }
      free (ff);
    }
  }

  /* Done parsing the body.  Now check the dates we (hopefully) parsed.  */
  if (!server_date || !*server_date) {
    xasprintf (msg, _("%sServer date unknown, "), *msg);
    date_result = max_state_alt(STATE_UNKNOWN, date_result);
  } else if (!document_date || !*document_date) {
    xasprintf (msg, _("%sDocument modification date unknown, "), *msg);
    date_result = max_state_alt(STATE_CRITICAL, date_result);
  } else {
    time_t srv_data = parse_time_string (server_date);
    time_t doc_data = parse_time_string (document_date);

    if (srv_data <= 0) {
      xasprintf (msg, _("%sServer date \"%100s\" unparsable, "), *msg, server_date);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data <= 0) {
      xasprintf (msg, _("%sDocument date \"%100s\" unparsable, "), *msg, document_date);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data > srv_data + 30) {
      xasprintf (msg, _("%sDocument is %d seconds in the future, "), *msg, (int)doc_data - (int)srv_data);
      date_result = max_state_alt(STATE_CRITICAL, date_result);
    } else if (doc_data < srv_data - maximum_age) {
      int n = (srv_data - doc_data);
      if (n > (60 * 60 * 24 * 2)) {
        xasprintf (msg, _("%sLast modified %.1f days ago, "), *msg, ((float) n) / (60 * 60 * 24));
        date_result = max_state_alt(STATE_CRITICAL, date_result);
      } else {
        xasprintf (msg, _("%sLast modified %d:%02d:%02d ago, "), *msg, n / (60 * 60), (n / 60) % 60, n % 60);
        date_result = max_state_alt(STATE_CRITICAL, date_result);
      }
    }
    free (server_date);
    free (document_date);
  }
  return date_result;
}

int
get_content_length (const char *headers)
{
  const char *s;
  int content_length = 0;

  s = headers;
  while (*s) {
    const char *field = s;
    const char *value = 0;

    /* Find the end of the header field */
    while (*s && !isspace(*s) && *s != ':')
      s++;

    /* Remember the header value, if any. */
    if (*s == ':')
      value = ++s;

    /* Skip to the end of the header, including continuation lines. */
    while (*s && !(*s == '\n' && (s[1] != ' ' && s[1] != '\t')))
      s++;

    /* Avoid stepping over end-of-string marker */
    if (*s)
      s++;

    /* Process this header. */
    if (value && value > field+2) {
      char *ff = (char *) malloc (value-field);
      char *ss = ff;
      while (field < value-1)
        *ss++ = tolower(*field++);
      *ss++ = 0;

      if (!strcmp (ff, "content-length")) {
        const char *e;
        while (*value && isspace (*value))
          value++;
        for (e = value; *e && *e != '\r' && *e != '\n'; e++)
          ;
        ss = (char *) malloc (e - value + 1);
        strncpy (ss, value, e - value);
        ss[e - value] = 0;
        content_length = atoi(ss);
        free (ss);
      }
      free (ff);
    }
  }
  return (content_length);
}

char *
prepend_slash (char *path)
{
  char *newpath;

  if (path[0] == '/')
    return path;

  if ((newpath = malloc (strlen(path) + 2)) == NULL)
    die (STATE_UNKNOWN, _("HTTP UNKNOWN - Memory allocation error\n"));
  newpath[0] = '/';
  strcpy (newpath + 1, path);
  free (path);
  return newpath;
}

int
check_http (void)
{
  char *msg;
  char *status_line;
  char *status_code;
  char *header;
  char *page;
  char *auth;
  int http_status;
  int i = 0;
  size_t pagesize = 0;
  char *full_page;
  char *full_page_new;
  char *buf;
  char *pos;
  long microsec;
  double elapsed_time;
  int page_len = 0;
  int result = STATE_OK;

  /* try to connect to the host at the given port number */
  if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
    die (STATE_CRITICAL, _("HTTP CRITICAL - Unable to open TCP socket\n"));
#ifdef HAVE_SSL
  if (use_ssl == TRUE) {
    result = np_net_ssl_init_with_hostname_and_version(sd, (use_sni ? host_name : NULL), ssl_version);
    if (result != STATE_OK)
      return result;
    if (check_cert == TRUE) {
      result = np_net_ssl_check_cert(days_till_exp_warn, days_till_exp_crit);
      np_net_ssl_cleanup();
      if (sd) close(sd);
      return result;
    }
  }
#endif /* HAVE_SSL */

  xasprintf (&buf, "%s %s %s\r\n%s\r\n", http_method, server_url, host_name ? "HTTP/1.1" : "HTTP/1.0", user_agent);

  /* tell HTTP/1.1 servers not to keep the connection alive */
  xasprintf (&buf, "%sConnection: close\r\n", buf);

  /* optionally send the host header info */
  if (host_name) {
    /*
     * Specify the port only if we're using a non-default port (see RFC 2616,
     * 14.23).  Some server applications/configurations cause trouble if the
     * (default) port is explicitly specified in the "Host:" header line.
     */
    if ((use_ssl == FALSE && server_port == HTTP_PORT) ||
        (use_ssl == TRUE && server_port == HTTPS_PORT))
      xasprintf (&buf, "%sHost: %s\r\n", buf, host_name);
    else
      xasprintf (&buf, "%sHost: %s:%d\r\n", buf, host_name, server_port);
  }

  /* optionally send any other header tag */
  if (http_opt_headers_count) {
    for (i = 0; i < http_opt_headers_count ; i++) {
      for ((pos = strtok(http_opt_headers[i], INPUT_DELIMITER)); pos; (pos = strtok(NULL, INPUT_DELIMITER)))
        xasprintf (&buf, "%s%s\r\n", buf, pos);
    }
    /* This cannot be free'd here because a redirection will then try to access this and segfault */
    /* Covered in a testcase in tests/check_http.t */
    /* free(http_opt_headers); */
  }

  /* optionally send the authentication info */
  if (strlen(user_auth)) {
    base64_encode_alloc (user_auth, strlen (user_auth), &auth);
    xasprintf (&buf, "%sAuthorization: Basic %s\r\n", buf, auth);
  }

  /* optionally send the proxy authentication info */
  if (strlen(proxy_auth)) {
    base64_encode_alloc (proxy_auth, strlen (proxy_auth), &auth);
    xasprintf (&buf, "%sProxy-Authorization: Basic %s\r\n", buf, auth);
  }

  /* either send http POST data (any data, not only POST)*/
  if (http_post_data) {
    if (http_content_type) {
      xasprintf (&buf, "%sContent-Type: %s\r\n", buf, http_content_type);
    } else {
      xasprintf (&buf, "%sContent-Type: application/x-www-form-urlencoded\r\n", buf);
    }

    xasprintf (&buf, "%sContent-Length: %i\r\n\r\n", buf, (int)strlen (http_post_data));
    xasprintf (&buf, "%s%s%s", buf, http_post_data, CRLF);
  }
  else {
    /* or just a newline so the server knows we're done with the request */
    xasprintf (&buf, "%s%s", buf, CRLF);
  }

  if (verbose) printf ("%s\n", buf);
  my_send (buf, strlen (buf));

  /* fetch the page */
  full_page = strdup("");
  while ((i = my_recv (buffer, MAX_INPUT_BUFFER-1)) > 0) {
    buffer[i] = '\0';
    xasprintf (&full_page_new, "%s%s", full_page, buffer);
    free (full_page);
    full_page = full_page_new;
    pagesize += i;

                if (no_body && document_headers_done (full_page)) {
                  i = 0;
                  break;
                }
  }

  if (i < 0 && errno != ECONNRESET) {
#ifdef HAVE_SSL
    /*
    if (use_ssl) {
      sslerr=SSL_get_error(ssl, i);
      if ( sslerr == SSL_ERROR_SSL ) {
        die (STATE_WARNING, _("HTTP WARNING - Client Certificate Required\n"));
      } else {
        die (STATE_CRITICAL, _("HTTP CRITICAL - Error on receive\n"));
      }
    }
    else {
    */
#endif
      die (STATE_CRITICAL, _("HTTP CRITICAL - Error on receive\n"));
#ifdef HAVE_SSL
      /* XXX
    }
    */
#endif
  }

  /* return a CRITICAL status if we couldn't read any data */
  if (pagesize == (size_t) 0)
    die (STATE_CRITICAL, _("HTTP CRITICAL - No data received from host\n"));

  /* close the connection */
#ifdef HAVE_SSL
  np_net_ssl_cleanup();
#endif
  if (sd) close(sd);

  /* Save check time */
  microsec = deltime (tv);
  elapsed_time = (double)microsec / 1.0e6;

  /* leave full_page untouched so we can free it later */
  page = full_page;

  if (verbose)
    printf ("%s://%s:%d%s is %d characters\n",
      use_ssl ? "https" : "http", server_address,
      server_port, server_url, (int)pagesize);

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
    printf ("**** HEADER ****\n%s\n**** CONTENT ****\n%s\n", header,
                (no_body ? "  [[ skipped ]]" : page));

  /* make sure the status line matches the response we are looking for */
  if (!expected_statuscode (status_line, server_expect)) {
    if (server_port == HTTP_PORT)
      xasprintf (&msg,
                _("Invalid HTTP response received from host: %s\n"),
                status_line);
    else
      xasprintf (&msg,
                _("Invalid HTTP response received from host on port %d: %s\n"),
                server_port, status_line);
    die (STATE_CRITICAL, "HTTP CRITICAL - %s", msg);
  }

  /* Bypass normal status line check if server_expect was set by user and not default */
  /* NOTE: After this if/else block msg *MUST* be an asprintf-allocated string */
  if ( server_expect_yn  )  {
    xasprintf (&msg,
              _("Status line output matched \"%s\" - "), server_expect);
    if (verbose)
      printf ("%s\n",msg);
  }
  else {
    /* Status-Line = HTTP-Version SP Status-Code SP Reason-Phrase CRLF */
    /* HTTP-Version   = "HTTP" "/" 1*DIGIT "." 1*DIGIT */
    /* Status-Code = 3 DIGITS */

    status_code = strchr (status_line, ' ') + sizeof (char);
    if (strspn (status_code, "1234567890") != 3)
      die (STATE_CRITICAL, _("HTTP CRITICAL: Invalid Status Line (%s)\n"), status_line);

    http_status = atoi (status_code);

    /* check the return code */

    if (http_status >= 600 || http_status < 100) {
      die (STATE_CRITICAL, _("HTTP CRITICAL: Invalid Status (%s)\n"), status_line);
    }
    /* server errors result in a critical state */
    else if (http_status >= 500) {
      xasprintf (&msg, _("%s - "), status_line);
      result = STATE_CRITICAL;
    }
    /* client errors result in a warning state */
    else if (http_status >= 400) {
      xasprintf (&msg, _("%s - "), status_line);
      result = max_state_alt(STATE_WARNING, result);
    }
    /* check redirected page if specified */
    else if (http_status >= 300) {

      if (onredirect == STATE_DEPENDENT)
        redir (header, status_line);
      else
        result = max_state_alt(onredirect, result);
      xasprintf (&msg, _("%s - "), status_line);
    } /* end if (http_status >= 300) */
    else {
      /* Print OK status anyway */
      xasprintf (&msg, _("%s - "), status_line);
    }

  } /* end else (server_expect_yn)  */

  /* reset the alarm - must be called *after* redir or we'll never die on redirects! */
  alarm (0);

  if (maximum_age >= 0) {
    result = max_state_alt(check_document_dates(header, &msg), result);
  }

  /* Page and Header content checks go here */

  if (strlen (string_expect)) {
    if (!strstr (page, string_expect)) {
      strncpy(&output_string_search[0],string_expect,sizeof(output_string_search));
      if(output_string_search[sizeof(output_string_search)-1]!='\0') {
        bcopy("...",&output_string_search[sizeof(output_string_search)-4],4);
      }
      xasprintf (&msg, _("%sstring '%s' not found on '%s://%s:%d%s', "), msg, output_string_search, use_ssl ? "https" : "http", host_name ? host_name : server_address, server_port, server_url);
      result = STATE_CRITICAL;
    }
  }

  if (strlen (regexp)) {
    errcode = regexec (&preg, page, REGS, pmatch, 0);
    if ((errcode == 0 && invert_regex == 0) || (errcode == REG_NOMATCH && invert_regex == 1)) {
      /* OK - No-op to avoid changing the logic around it */
      result = max_state_alt(STATE_OK, result);
    }
    else if ((errcode == REG_NOMATCH && invert_regex == 0) || (errcode == 0 && invert_regex == 1)) {
      if (invert_regex == 0)
        xasprintf (&msg, _("%spattern not found, "), msg);
      else
        xasprintf (&msg, _("%spattern found, "), msg);
      result = STATE_CRITICAL;
    }
    else {
      /* FIXME: Shouldn't that be UNKNOWN? */
      regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
      xasprintf (&msg, _("%sExecute Error: %s, "), msg, errbuf);
      result = STATE_CRITICAL;
    }
  }

  /* make sure the page is of an appropriate size */
  /* page_len = get_content_length(header); */
  /* FIXME: Will this work with -N ? IMHO we should use
   * get_content_length(header) and always check if it's different than the
   * returned pagesize
   */
  /* FIXME: IIRC pagesize returns headers - shouldn't we make
   * it == get_content_length(header) ??
   */
  page_len = pagesize;
  if ((max_page_len > 0) && (page_len > max_page_len)) {
    xasprintf (&msg, _("%spage size %d too large, "), msg, page_len);
    result = max_state_alt(STATE_WARNING, result);
  } else if ((min_page_len > 0) && (page_len < min_page_len)) {
    xasprintf (&msg, _("%spage size %d too small, "), msg, page_len);
    result = max_state_alt(STATE_WARNING, result);
  }

  /* Cut-off trailing characters */
  if(msg[strlen(msg)-2] == ',')
    msg[strlen(msg)-2] = '\0';
  else
    msg[strlen(msg)-3] = '\0';

  /* check elapsed time */
  xasprintf (&msg,
            _("%s - %d bytes in %.3f second response time %s|%s %s"),
            msg, page_len, elapsed_time,
            (display_html ? "</A>" : ""),
            perfd_time (elapsed_time), perfd_size (page_len));

  result = max_state_alt(get_status(elapsed_time, thlds), result);

  die (result, "HTTP %s: %s\n", state_text(result), msg);
  /* die failed? */
  return STATE_UNKNOWN;
}



/* per RFC 2396 */
#define URI_HTTP "%5[HTPShtps]"
#define URI_HOST "%255[-.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define URI_PORT "%6d" /* MAX_PORT's width is 5 chars, 6 to detect overflow */
#define URI_PATH "%[-_.!~*'();/?:@&=+$,%#abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789]"
#define HD1 URI_HTTP "://" URI_HOST ":" URI_PORT "/" URI_PATH
#define HD2 URI_HTTP "://" URI_HOST "/" URI_PATH
#define HD3 URI_HTTP "://" URI_HOST ":" URI_PORT
#define HD4 URI_HTTP "://" URI_HOST
#define HD5 URI_PATH

void
redir (char *pos, char *status_line)
{
  int i = 0;
  char *x;
  char xx[2];
  char type[6];
  char *addr;
  char *url;

  addr = malloc (MAX_IPV4_HOSTLENGTH + 1);
  if (addr == NULL)
    die (STATE_UNKNOWN, _("HTTP UNKNOWN - Could not allocate addr\n"));

  url = malloc (strcspn (pos, "\r\n"));
  if (url == NULL)
    die (STATE_UNKNOWN, _("HTTP UNKNOWN - Could not allocate URL\n"));

  while (pos) {
    sscanf (pos, "%1[Ll]%*1[Oo]%*1[Cc]%*1[Aa]%*1[Tt]%*1[Ii]%*1[Oo]%*1[Nn]:%n", xx, &i);
    if (i == 0) {
      pos += (size_t) strcspn (pos, "\r\n");
      pos += (size_t) strspn (pos, "\r\n");
      if (strlen(pos) == 0)
        die (STATE_UNKNOWN,
             _("HTTP UNKNOWN - Could not find redirect location - %s%s\n"),
             status_line, (display_html ? "</A>" : ""));
      continue;
    }

    pos += i;
    pos += strspn (pos, " \t");

    /*
     * RFC 2616 (4.2):  ``Header fields can be extended over multiple lines by
     * preceding each extra line with at least one SP or HT.''
     */
    for (; (i = strspn (pos, "\r\n")); pos += i) {
      pos += i;
      if (!(i = strspn (pos, " \t"))) {
        die (STATE_UNKNOWN, _("HTTP UNKNOWN - Empty redirect location%s\n"),
             display_html ? "</A>" : "");
      }
    }

    url = realloc (url, strcspn (pos, "\r\n") + 1);
    if (url == NULL)
      die (STATE_UNKNOWN, _("HTTP UNKNOWN - Could not allocate URL\n"));

    /* URI_HTTP, URI_HOST, URI_PORT, URI_PATH */
    if (sscanf (pos, HD1, type, addr, &i, url) == 4) {
      url = prepend_slash (url);
      use_ssl = server_type_check (type);
    }

    /* URI_HTTP URI_HOST URI_PATH */
    else if (sscanf (pos, HD2, type, addr, url) == 3 ) {
      url = prepend_slash (url);
      use_ssl = server_type_check (type);
      i = server_port_check (use_ssl);
    }

    /* URI_HTTP URI_HOST URI_PORT */
    else if (sscanf (pos, HD3, type, addr, &i) == 3) {
      strcpy (url, HTTP_URL);
      use_ssl = server_type_check (type);
    }

    /* URI_HTTP URI_HOST */
    else if (sscanf (pos, HD4, type, addr) == 2) {
      strcpy (url, HTTP_URL);
      use_ssl = server_type_check (type);
      i = server_port_check (use_ssl);
    }

    /* URI_PATH */
    else if (sscanf (pos, HD5, url) == 1) {
      /* relative url */
      if ((url[0] != '/')) {
        if ((x = strrchr(server_url, '/')))
          *x = '\0';
        xasprintf (&url, "%s/%s", server_url, url);
      }
      i = server_port;
      strcpy (type, server_type);
      strcpy (addr, host_name ? host_name : server_address);
    }

    else {
      die (STATE_UNKNOWN,
           _("HTTP UNKNOWN - Could not parse redirect location - %s%s\n"),
           pos, (display_html ? "</A>" : ""));
    }

    break;

  } /* end while (pos) */

  if (++redir_depth > max_depth)
    die (STATE_WARNING,
         _("HTTP WARNING - maximum redirection depth %d exceeded - %s://%s:%d%s%s\n"),
         max_depth, type, addr, i, url, (display_html ? "</A>" : ""));

  if (server_port==i &&
      !strcmp(server_address, addr) &&
      (host_name && !strcmp(host_name, addr)) &&
      !strcmp(server_url, url))
    die (STATE_WARNING,
         _("HTTP WARNING - redirection creates an infinite loop - %s://%s:%d%s%s\n"),
         type, addr, i, url, (display_html ? "</A>" : ""));

  strcpy (server_type, type);

  free (host_name);
  host_name = strdup (addr);

  if (!(followsticky & STICKY_HOST)) {
    free (server_address);
    server_address = strdup (addr);
  }
  if (!(followsticky & STICKY_PORT)) {
    server_port = i;
  }

  free (server_url);
  server_url = url;

  if (server_port > MAX_PORT)
    die (STATE_UNKNOWN,
         _("HTTP UNKNOWN - Redirection to port above %d - %s://%s:%d%s%s\n"),
         MAX_PORT, server_type, server_address, server_port, server_url,
         display_html ? "</A>" : "");

  if (verbose)
    printf (_("Redirection to %s://%s:%d%s\n"), server_type,
            host_name ? host_name : server_address, server_port, server_url);

  check_http ();
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

char *perfd_time (double elapsed_time)
{
  return fperfdata ("time", elapsed_time, "s",
            thlds->warning?TRUE:FALSE, thlds->warning?thlds->warning->end:0,
            thlds->critical?TRUE:FALSE, thlds->critical?thlds->critical->end:0,
                   TRUE, 0, FALSE, 0);
}



char *perfd_size (int page_len)
{
  return perfdata ("size", page_len, "B",
            (min_page_len>0?TRUE:FALSE), min_page_len,
            (min_page_len>0?TRUE:FALSE), 0,
            TRUE, 0, FALSE, 0);
}

void
print_help (void)
{
  print_revision (progname, NP_VERSION);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf (COPYRIGHT, copyright, email);

  print_help_head(&resource_meta);

  printf ("\n\n");

  print_usage ();

  printf (_("NOTE: One or both of -H and -I must be specified"));

  printf ("\n");

  printf (UT_HELP_VRSN);
  print_parameters_help(options_help);

  printf (UT_VERBOSE);

  printf ("\n");
  printf ("%s\n", _("Notes:"));
  printf (" %s\n", _("This plugin will attempt to open an HTTP connection with the host."));
  printf (" %s\n", _("Successful connects return STATE_OK, refusals and timeouts return STATE_CRITICAL"));
  printf (" %s\n", _("other errors return STATE_UNKNOWN.  Successful connects, but incorrect reponse"));
  printf (" %s\n", _("messages from the host result in STATE_WARNING return values.  If you are"));
  printf (" %s\n", _("checking a virtual server that uses 'host headers' you must supply the FQDN"));
  printf (" %s\n", _("(fully qualified domain name) as the [host_name] argument."));

#ifdef HAVE_SSL
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
  printf (" %s\n\n", "CHECK CONTENT: check_http -w 5 -c 10 --ssl -H www.verisign.com");
  printf (" %s\n", _("When the 'www.verisign.com' server returns its content within 5 seconds,"));
  printf (" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
  printf (" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
  printf (" %s\n", _("a STATE_CRITICAL will be returned."));
  printf ("\n");
  printf (" %s\n\n", "CHECK CERTIFICATE: check_http -H www.verisign.com -C 14");
  printf (" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 14 days,"));
  printf (" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
  printf (" %s\n", _("14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when"));
  printf (" %s\n", _("the certificate is expired."));
  printf ("\n");
  printf (" %s\n\n", "CHECK CERTIFICATE: check_http -H www.verisign.com -C 30,14");
  printf (" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 30 days,"));
  printf (" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
  printf (" %s\n", _("30 days, but more than 14 days, a STATE_WARNING is returned."));
  printf (" %s\n", _("A STATE_CRITICAL will be returned when certificate expires in less than 14 days"));

#endif

  printf (UT_SUPPORT);

}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf (" %s -H <vhost> | -I <IP-address> [-u <uri>] [-p <port>]\n",progname);
  printf ("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L] [-a auth]\n");
  printf ("       [-b proxy_auth] [-f <ok|warning|critcal|follow|sticky|stickyport>]\n");
  printf ("       [-e <expect>] [-s string] [-l] [-r <regex> | -R <case-insensitive regex>]\n");
  printf ("       [-P string] [-m <min_pg_size>:<max_pg_size>] [-4|-6] [-N] [-M <age>]\n");
  printf ("       [-A string] [-k string] [-S <version>] [--sni] [-C <warn_age>[,<crit_age>]]\n");
  printf ("       [-T <content-type>] [-j method]\n");
  printf (" %s --metadata\n", progname);
}
