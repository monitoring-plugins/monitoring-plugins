/******************************************************************************
*
* Nagios check_http plugin
*
* License: GPL
* Copyright (c) 1999-2006 nagios-plugins team
*
* Last Modified: $Date$
*
* Description:
*
* This file contains the check_http plugin
*
*  This plugin tests the HTTP service on the specified host. It can test
*  normal (http) and secure (https) servers, follow redirects, search for
*  strings and regular expressions, check connection times, and report on
*  certificate expiration times.
*
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

 $Id$
 
******************************************************************************/
/* splint -I. -I../../plugins -I../../lib/ -I/usr/kerberos/include/ ../../plugins/check_http.c */

const char *progname = "check_http";
const char *revision = "$Revision$";
const char *copyright = "1999-2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

#define INPUT_DELIMITER ";"

#define HTTP_EXPECT "HTTP/1."
enum {
  MAX_IPV4_HOSTLENGTH = 255,
  HTTP_PORT = 80,
  HTTPS_PORT = 443
};

#ifdef HAVE_SSL
int check_cert = FALSE;
int days_till_exp;
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
char **http_opt_headers;
int http_opt_headers_count = 0;
int onredirect = STATE_OK;
int use_ssl = FALSE;
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
static char *base64 (const char *bin, size_t len);
int check_http (void);
void redir (char *pos, char *status_line);
int server_type_check(const char *type);
int server_port_check(int ssl_flag);
char *perfd_time (double microsec);
char *perfd_size (int page_len);
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
    usage4 (_("Could not parse arguments"));

  if (strstr (timestamp, ":")) {
    if (strstr (server_url, "?"))
      asprintf (&server_url, "%s&%s", server_url, timestamp);
    else
      asprintf (&server_url, "%s?%s", server_url, timestamp);
  }

  if (display_html == TRUE)
    printf ("<A HREF=\"%s://%s:%d%s\" target=\"_blank\">", 
      use_ssl ? "https" : "http", host_name,
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

  enum {
    INVERT_REGEX = CHAR_MAX + 1
  };

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
    {"url", required_argument, 0, 'u'},
    {"string", required_argument, 0, 's'},
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
    c = getopt_long (argc, argv, "Vvh46t:c:w:A:k:H:P:T:I:a:e:p:s:R:r:u:f:C:nlLSm:M:N", longopts, &option);
    if (c == -1 || c == EOF)
      break;

    switch (c) {
    case '?': /* usage */
      usage2 (_("Unknown argument"), optarg);
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
        usage2 (_("Timeout interval must be a positive integer"), optarg);
      else
        socket_timeout = atoi (optarg);
      break;
    case 'c': /* critical time threshold */
      if (!is_nonnegative (optarg))
        usage2 (_("Critical threshold must be integer"), optarg);
      else {
        critical_time = strtod (optarg, NULL);
        check_critical_time = TRUE;
      }
      break;
    case 'w': /* warning time threshold */
      if (!is_nonnegative (optarg))
        usage2 (_("Warning threshold must be integer"), optarg);
      else {
        warning_time = strtod (optarg, NULL);
        check_warning_time = TRUE;
      }
      break;
    case 'A': /* User Agent String */
      asprintf (&user_agent, "User-Agent: %s", optarg);
      break;
    case 'k': /* Additional headers */
      if (http_opt_headers_count == 0)
        http_opt_headers = malloc (sizeof (char *) * (++http_opt_headers_count));
      else
        http_opt_headers = realloc (http_opt_headers, sizeof (char *) * (++http_opt_headers_count));
      http_opt_headers[http_opt_headers_count - 1] = optarg;
      /* asprintf (&http_opt_headers, "%s", optarg); */
      break;
    case 'L': /* show html link */
      display_html = TRUE;
      break;
    case 'n': /* do not show html link */
      display_html = FALSE;
      break;
    case 'C': /* Check SSL cert validity */
#ifdef HAVE_SSL
      if (!is_intnonneg (optarg))
        usage2 (_("Invalid certificate expiration period"), optarg);
      else {
        days_till_exp = atoi (optarg);
        check_cert = TRUE;
      }
     /* Fall through to -S option */
#endif
    case 'S': /* use SSL */
#ifndef HAVE_SSL
      usage4 (_("Invalid option - SSL is not available"));
#endif
      use_ssl = TRUE;
      if (specify_port == FALSE)
        server_port = HTTPS_PORT;
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
      if (strstr (optarg, ":"))
        sscanf (optarg, "%*[^:]:%d", &server_port);
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
    case 'T': /* Content-type */
      asprintf (&http_content_type, "%s", optarg);
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



static void
check_document_dates (const char *headers)
{
  const char *s;
  char *server_date = 0;
  char *document_date = 0;

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
    die (STATE_UNKNOWN, _("Server date unknown\n"));
  } else if (!document_date || !*document_date) {
    die (STATE_CRITICAL, _("Document modification date unknown\n"));
  } else {
    time_t srv_data = parse_time_string (server_date);
    time_t doc_data = parse_time_string (document_date);

    if (srv_data <= 0) {
      die (STATE_CRITICAL, _("CRITICAL - Server date \"%100s\" unparsable"), server_date);
    } else if (doc_data <= 0) {
      die (STATE_CRITICAL, _("CRITICAL - Document date \"%100s\" unparsable"), document_date);
    } else if (doc_data > srv_data + 30) {
      die (STATE_CRITICAL, _("CRITICAL - Document is %d seconds in the future\n"), (int)doc_data - (int)srv_data);
    } else if (doc_data < srv_data - maximum_age) {
    int n = (srv_data - doc_data);
    if (n > (60 * 60 * 24 * 2))
      die (STATE_CRITICAL,
        _("CRITICAL - Last modified %.1f days ago\n"),
        ((float) n) / (60 * 60 * 24));
  else
    die (STATE_CRITICAL,
        _("CRITICAL - Last modified %d:%02d:%02d ago\n"),
        n / (60 * 60), (n / 60) % 60, n % 60);
    }

    free (server_date);
    free (document_date);
  }
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
  char *buf;
  char *pos;
  long microsec;
  double elapsed_time;
  int page_len = 0;
  int result = STATE_UNKNOWN;

  /* try to connect to the host at the given port number */
  if (my_tcp_connect (server_address, server_port, &sd) != STATE_OK)
    die (STATE_CRITICAL, _("Unable to open TCP socket\n"));
#ifdef HAVE_SSL
  if (use_ssl == TRUE) {
    np_net_ssl_init(sd);
    if (check_cert == TRUE) {
      result = np_net_ssl_check_cert(days_till_exp);
      np_net_ssl_cleanup();
      if(sd) close(sd);
      return result;
    }
  }
#endif /* HAVE_SSL */

  asprintf (&buf, "%s %s HTTP/1.0\r\n%s\r\n", http_method, server_url, user_agent);

  /* optionally send the host header info */
  if (host_name)
    asprintf (&buf, "%sHost: %s\r\n", buf, host_name);

  /* optionally send any other header tag */
  if (http_opt_headers_count) {
    for (i = 0; i < http_opt_headers_count ; i++) {
      for ((pos = strtok(http_opt_headers[i], INPUT_DELIMITER)); pos; (pos = strtok(NULL, INPUT_DELIMITER)))
        asprintf (&buf, "%s%s\r\n", buf, pos);
    }
    free(http_opt_headers);
  }

  /* optionally send the authentication info */
  if (strlen(user_auth)) {
    auth = base64 (user_auth, strlen (user_auth));
    asprintf (&buf, "%sAuthorization: Basic %s\r\n", buf, auth);
  }

  /* either send http POST data */
  if (http_post_data) {
    if (http_content_type) {
      asprintf (&buf, "%sContent-Type: %s\r\n", buf, http_content_type);
    } else {
      asprintf (&buf, "%sContent-Type: application/x-www-form-urlencoded\r\n", buf);
    }
    
    asprintf (&buf, "%sContent-Length: %i\r\n\r\n", buf, (int)strlen (http_post_data));
    asprintf (&buf, "%s%s%s", buf, http_post_data, CRLF);
  }
  else {
    /* or just a newline so the server knows we're done with the request */
    asprintf (&buf, "%s%s", buf, CRLF);
  }

  if (verbose) printf ("%s\n", buf);
  my_send (buf, strlen (buf));

  /* fetch the page */
  full_page = strdup("");
  while ((i = my_recv (buffer, MAX_INPUT_BUFFER-1)) > 0) {
    buffer[i] = '\0';
    asprintf (&full_page, "%s%s", full_page, buffer);
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
        die (STATE_WARNING, _("Client Certificate Required\n"));
      } else {
        die (STATE_CRITICAL, _("Error on receive\n"));
      }
    }
    else {
    */
#endif
      die (STATE_CRITICAL, _("Error on receive\n"));
#ifdef HAVE_SSL
      /* XXX
    }
    */
#endif
  }

  /* return a CRITICAL status if we couldn't read any data */
  if (pagesize == (size_t) 0)
    die (STATE_CRITICAL, _("No data received %s\n"), timestamp);

  /* close the connection */
#ifdef HAVE_SSL
  np_net_ssl_cleanup();
#endif
  if(sd) close(sd);

  /* reset the alarm */
  alarm (0);

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
  if (!strstr (status_line, server_expect)) {
    if (server_port == HTTP_PORT)
      asprintf (&msg,
                _("Invalid HTTP response received from host\n"));
    else
      asprintf (&msg,
                _("Invalid HTTP response received from host on port %d\n"),
                server_port);
    die (STATE_CRITICAL, "%s", msg);
  }

  /* Exit here if server_expect was set by user and not default */
  if ( server_expect_yn  )  {
    asprintf (&msg,
              _("HTTP OK: Status line output matched \"%s\"\n"),
              server_expect);
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

    if (http_status >= 600 || http_status < 100)
      die (STATE_CRITICAL, _("HTTP CRITICAL: Invalid Status (%s)\n"), status_line);

    /* server errors result in a critical state */
    else if (http_status >= 500)
      die (STATE_CRITICAL, _("HTTP CRITICAL: %s\n"), status_line);

    /* client errors result in a warning state */
    else if (http_status >= 400)
      die (STATE_WARNING, _("HTTP WARNING: %s\n"), status_line);

    /* check redirected page if specified */
    else if (http_status >= 300) {

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
      die (onredirect,
           _(" - %s - %.3f second response time %s%s|%s %s\n"),
           status_line, elapsed_time, timestamp,
           (display_html ? "</A>" : ""),
           perfd_time (elapsed_time), perfd_size (pagesize));
    } /* end if (http_status >= 300) */

  } /* end else (server_expect_yn)  */
    
        if (maximum_age >= 0) {
          check_document_dates (header);
        }

  /* check elapsed time */
  microsec = deltime (tv);
  elapsed_time = (double)microsec / 1.0e6;
  asprintf (&msg,
            _("HTTP WARNING: %s - %.3f second response time %s%s|%s %s\n"),
            status_line, elapsed_time, timestamp,
            (display_html ? "</A>" : ""),
            perfd_time (elapsed_time), perfd_size (pagesize));
  if (check_critical_time == TRUE && elapsed_time > critical_time)
    die (STATE_CRITICAL, "%s", msg);
  if (check_warning_time == TRUE && elapsed_time > warning_time)
    die (STATE_WARNING, "%s", msg);

  /* Page and Header content checks go here */
  /* these checks should be last */

  if (strlen (string_expect)) {
    if (strstr (page, string_expect)) {
      printf (_("HTTP OK %s - %.3f second response time %s%s|%s %s\n"),
              status_line, elapsed_time,
              timestamp, (display_html ? "</A>" : ""),
              perfd_time (elapsed_time), perfd_size (pagesize));
      exit (STATE_OK);
    }
    else {
      printf (_("CRITICAL - string not found%s|%s %s\n"),
              (display_html ? "</A>" : ""),
              perfd_time (elapsed_time), perfd_size (pagesize));
      exit (STATE_CRITICAL);
    }
  }

  if (strlen (regexp)) {
    errcode = regexec (&preg, page, REGS, pmatch, 0);
    if ((errcode == 0 && invert_regex == 0) || (errcode == REG_NOMATCH && invert_regex == 1)) {
      printf (_("HTTP OK %s - %.3f second response time %s%s|%s %s\n"),
              status_line, elapsed_time,
              timestamp, (display_html ? "</A>" : ""),
              perfd_time (elapsed_time), perfd_size (pagesize));
      exit (STATE_OK);
    }
    else if ((errcode == REG_NOMATCH && invert_regex == 0) || (errcode == 0 && invert_regex == 1)) {
      if (invert_regex == 0) 
        msg = strdup(_("pattern not found"));
      else 
        msg = strdup(_("pattern found"));
      printf (_("%s - %s%s|%s %s\n"),
        _("CRITICAL"),
        msg,
        (display_html ? "</A>" : ""),
        perfd_time (elapsed_time), perfd_size (pagesize));
      exit (STATE_CRITICAL);
    }
    else {
      regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
      printf (_("CRITICAL - Execute Error: %s\n"), errbuf);
      exit (STATE_CRITICAL);
    }
  }

  /* make sure the page is of an appropriate size */
  /* page_len = get_content_length(header); */
  page_len = pagesize;
  if ((max_page_len > 0) && (page_len > max_page_len)) {
    printf (_("HTTP WARNING: page size %d too large%s|%s\n"),
      page_len, (display_html ? "</A>" : ""), perfd_size (page_len) );
    exit (STATE_WARNING);
  } else if ((min_page_len > 0) && (page_len < min_page_len)) {
    printf (_("HTTP WARNING: page size %d too small%s|%s\n"),
      page_len, (display_html ? "</A>" : ""), perfd_size (page_len) );
    exit (STATE_WARNING);
  }
  /* We only get here if all tests have been passed */
  asprintf (&msg, _("HTTP OK %s - %d bytes in %.3f seconds %s%s|%s %s\n"),
            status_line, page_len, elapsed_time,
            timestamp, (display_html ? "</A>" : ""),
            perfd_time (elapsed_time), perfd_size (page_len));
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

void
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
    die (STATE_UNKNOWN, _("Could not allocate addr\n"));
  
  url = malloc (strcspn (pos, "\r\n"));
  if (url == NULL)
    die (STATE_UNKNOWN, _("Could not allocate url\n"));

  while (pos) {
    sscanf (pos, "%[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]:%n", xx, &i);
    if (i == 0) {
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
      die (STATE_UNKNOWN, _("could not allocate url\n"));

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
        if ((x = strrchr(server_url, '/')))
          *x = '\0';
        asprintf (&url, "%s/%s", server_url, url);
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
            check_warning_time, warning_time,
            check_critical_time, critical_time,
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
  print_revision (progname, revision);

  printf ("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
  printf (COPYRIGHT, copyright, email);

  printf ("%s\n", _("This plugin tests the HTTP service on the specified host. It can test"));
  printf ("%s\n", _("normal (http) and secure (https) servers, follow redirects, search for"));
  printf ("%s\n", _("strings and regular expressions, check connection times, and report on"));
  printf ("%s\n", _("certificate expiration times."));

  printf ("\n\n");

  print_usage ();

  printf (_("NOTE: One or both of -H and -I must be specified"));

  printf ("\n");

  printf (_(UT_HELP_VRSN));

  printf (" %s\n", "-H, --hostname=ADDRESS");
  printf ("    %s\n", _("Host name argument for servers using host headers (virtual host)"));
  printf ("    %s\n", _("Append a port to include it in the header (eg: example.com:5000)"));
  printf (" %s\n", "-I, --IP-address=ADDRESS");
  printf ("    %s\n", _("IP address or name (use numeric address if possible to bypass DNS lookup)."));
  printf (" %s\n", "-p, --port=INTEGER");
  printf (" %s", _("Port number (default: "));
  printf ("%d)\n", HTTP_PORT);

  printf (_(UT_IPv46));

#ifdef HAVE_SSL
  printf (" %s\n", "-S, --ssl");
  printf ("   %s\n", _("Connect via SSL"));
  printf (" %s\n", "-C, --certificate=INTEGER");
  printf ("   %s\n", _("Minimum number of days a certificate has to be valid."));
  printf ("   %s\n", _("(when this option is used the url is not checked.)\n"));
#endif

  printf (" %s\n", "-e, --expect=STRING");
  printf ("    %s\n", _("String to expect in first (status) line of server response (default: "));
  printf ("%s\n", HTTP_EXPECT);
  printf ("    %s\n", _("If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)"));
  printf (" %s\n", "-s, --string=STRING");
  printf ("    %s\n", _("String to expect in the content"));
  printf (" %s\n", "-u, --url=PATH");
  printf ("    %s\n", _("URL to GET or POST (default: /)"));
  printf (" %s\n," "-P, --post=STRING");
  printf ("    %s\n", _("URL encoded http POST data"));
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
  printf ("    %s\n", _(" Any other tags to be sent in http header. Use multiple times for additional headers"));
  printf (" %s\n", "-L, --link=URL");
  printf ("    %s\n", _("Wrap output in HTML link (obsoleted by urlize)"));
  printf (" %s\n", "-f, --onredirect=<ok|warning|critical|follow>");
  printf ("    %s\n", _("How to handle redirected pages"));
  printf (" %s\n", "-m, --pagesize=INTEGER<:INTEGER>");
  printf ("    %s\n", _("Minimum page size required (bytes) : Maximum page size required (bytes)"));

  printf (_(UT_WARN_CRIT));

  printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);

  printf (_(UT_VERBOSE));

  printf (_("Notes:"));
  printf (" %s\n", _("This plugin will attempt to open an HTTP connection with the host."));
  printf (" %s\n", _("Successful connects return STATE_OK, refusals and timeouts return STATE_CRITICAL"));
  printf (" %s\n", _("other errors return STATE_UNKNOWN.  Successful connects, but incorrect reponse"));
  printf (" %s\n", _("messages from the host result in STATE_WARNING return values.  If you are"));
  printf (" %s\n", _("checking a virtual server that uses 'host headers' you must supply the FQDN"));
  printf (" %s\n", _("(fully qualified domain name) as the [host_name] argument."));

#ifdef HAVE_SSL
  printf (" %s\n", _("This plugin can also check whether an SSL enabled web server is able to"));
  printf (" %s\n", _("serve content (optionally within a specified time) or whether the X509 "));
  printf (" %s\n", _("certificate is still valid for the specified number of days."));
  printf (_("Examples:"));
  printf (" %s\n\n", "CHECK CONTENT: check_http -w 5 -c 10 --ssl www.verisign.com");
  printf (" %s\n", _("When the 'www.verisign.com' server returns its content within 5 seconds,"));
  printf (" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
  printf (" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
  printf (" %s\n\n", _("a STATE_CRITICAL will be returned."));

  printf (" %s\n\n", "CHECK CERTIFICATE: check_http www.verisign.com -C 14");
  printf (" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 14 days,"));
  printf (" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
  printf (" %s\n", _("14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when"));
  printf (" %s\n\n", _("the certificate is expired."));
#endif

  printf (_(UT_SUPPORT));

}



void
print_usage (void)
{
  printf (_("Usage:"));
  printf (" %s -H <vhost> | -I <IP-address> [-u <uri>] [-p <port>]\n",progname);
  printf ("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L]\n");
  printf ("       [-a auth] [-f <ok | warn | critcal | follow>] [-e <expect>]\n");
  printf ("       [-s string] [-l] [-r <regex> | -R <case-insensitive regex>] [-P string]\n");
  printf ("       [-m <min_pg_size>:<max_pg_size>] [-4|-6] [-N] [-M <age>] [-A string] [-k string]\n");
}
