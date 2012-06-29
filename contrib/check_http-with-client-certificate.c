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
 *****************************************************************************/

/****************************************************************************
 *
 * check_http is derived from the original check_http provided by
 * Ethan Galstad/Karl DeBisschop
 *
 * This provides some additional functionality including:
 * - check server certificate against supplied hostname (Host: header) if any
 * - check server certificate against local CA certificates (as browsers do)
 * - authenticate with client certificate (and optional passphrase)
 * - specify HTTP returncodes to return a status of WARNING or OK instead of
 *   CRITICAL (only global for 3xx or 4xx errors)
 * - check only against HTTP status line and exit immediately if not matched
 *
 *****************************************************************************/

const char *progname = "check_http";
#define REVISION "$Revision: 1117 $"
#define CVSREVISION "1.24"
#define COPYRIGHT "2003"
#define AUTHORS "Fabian Pehla"
#define EMAIL "fabian@pehla.de"

#include "config.h"
#include "common.h"
#include "netutils.h"
#include "utils.h"


#define HELP_TXT_SUMMARY "\
This plugin tests the HTTP service on the specified host. It can test\n\
normal (http) and secure (https) servers, follow redirects, search for\n\
strings and regular expressions, check connection times, and report on\n\
certificate expiration times.\n"

#define HELP_TXT_OPTIONS "\
-H <virtual host> -I <ip address> [-p <port>] [-u <uri>]\n\
            [-w <warn time>] [-c <critical time>] [-t <timeout>]\n\
            [-S] [-C <days>] [-a <basic auth>] [-A <certificate file>]\n\
            [-Z <ca certificate file>] [-e <expect>] [-E <expect only>]\n\
            [-s <string>] [-r <regex>] [-R <regex case insensitive>]\n\
            [-f (ok|warn|critical|follow)] [-g (ok|warn|critical)]\n"

#define HELP_TXT_LONGOPTIONS "\
 -H, --hostname=<virtual host>\n\
    FQDN host name argument for use in HTTP Host:-Header (virtual host)\n\
    If used together wich the -S option, the server certificate will\n\
    be checked against this hostname\n\
 -I, --ip-address=<address>\n\
   IP address or hostname for TCP connect (use IP to avoid DNS lookup)\n\
 -p, --port=<port>\n\
   Port number (default: %d)\n\
 -u, --url-path=<uri>\n\
   URL to request from host (default: %s)\n\
 -S, --ssl\n\
   Use SSL (default port: %d)\n\
 -C, --server-certificate-days=<days>\n\
   Minimum number of days a server certificate must be valid\n\
   No other check can be combined with this option\n\
 -a, --basic-auth=<username:password>\n\
   Colon separated username and password for basic authentication\n\
 -A, --client-certificate=<certificate file>\n\
   File containing X509 client certificate and key\n\
 -K, --passphrase=<passphrase>\n\
   Passphrase for the client certificate key\n\
   This option can only be used in combination with the -A option\n\
 -Z, --ca-certificate=<certificate file>\n\
   File containing certificates of trusted CAs\n\
   The server certificate will be checked against these CAs\n\
 -e, --http-expect=<expect string>\n\
   String to expect in HTTP response line (Default: %s)\n\
 -E, --http-expect-only=<expect only string>\n\
   String to expect in HTTP response line\n\
   No other checks are made, this either matches the response\n\
   or exits immediately\n\
 -s, --content-string=<string>\n\
   String to expect in content\n\
 -r, --content-ereg=<regex>\n\
   Regular expression to expect in content\n\
 -R, --content-eregi=<regex case insensitive>\n\
   Case insensitive regular expression to expect in content\n\
 -f, --onredirect=(ok|warning|critical|follow)\n\
   Follow a redirect (3xx) or return with a user defined state\n\
   Default: OK\n\
 -g, --onerror=(ok|warning|critical)\n\
   Status to return on a client error (4xx)\n\
 -m, --min=INTEGER\n\
   Minimum page size required (bytes)\n\
 -t, --timeout=<timeout>\n\
   Seconds before connection times out (default: %d)\n\
 -c, --critical=<critical time>\n\
   Response time to result in critical status (seconds)\n\
 -w, --warning=<warn time>\n\
   Response time to result in warning status (seconds)\n\
 -V, --version\n\
    Print version information\n\
 -v, --verbose\n\
    Show details for command-line debugging (do not use with nagios server)\n\
 -h, --help\n\
    Print detailed help screen\n"



#define HTTP_PORT 80
#define DEFAULT_HTTP_URL_PATH "/"
#define DEFAULT_HTTP_EXPECT "HTTP/1."
#define DEFAULT_HTTP_METHOD "GET"
#define DEFAULT_HTTP_REDIRECT_STATE STATE_OK
#define DEFAULT_HTTP_CLIENT_ERROR_STATE STATE_WARNING

#define HTTP_TEMPLATE_REQUEST "%s%s %s HTTP/1.0\r\n"
#define HTTP_TEMPLATE_HEADER_USERAGENT "%sUser-Agent: %s/%s (nagios-plugins %s)\r\n"
#define HTTP_TEMPLATE_HEADER_HOST "%sHost: %s\r\n"
#define HTTP_TEMPLATE_HEADER_AUTH "%sAuthorization: Basic %s\r\n"

/* fill in printf with protocol_text(use_ssl), state_text(state), page->status, elapsed_time */
#define RESULT_TEMPLATE_STATUS_RESPONSE_TIME "%s %s: %s - %7.3f seconds response time|time=%7.3f\n"
#define RESULT_TEMPLATE_RESPONSE_TIME "%s %s: %7.3f seconds response time|time=%7.3f\n"

#ifdef HAVE_SSL

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

#define HTTPS_PORT 443
#endif 

#ifdef HAVE_REGEX_H
#include <regex.h>
#define REGEX_REGS 2
#define MAX_REGEX_SIZE 256
#endif

#define chk_protocol(protocol) ( strstr( protocol, "https" ) ? TRUE : FALSE );
#define protocol_std_port(use_ssl) ( use_ssl ? HTTPS_PORT : HTTP_PORT );
#define protocol_text(use_ssl) ( use_ssl ? "HTTPS" : "HTTP" )

#define MAX_IPV4_HOSTLENGTH 64
#define HTTP_HEADER_LOCATION_MATCH "%*[Ll]%*[Oo]%*[Cc]%*[Aa]%*[Tt]%*[Ii]%*[Oo]%*[Nn]: "
#define HTTP_HEADER_PROTOCOL_MATCH "%[HTPShtps]://"
#define HTTP_HEADER_HOSTNAME_MATCH "%[a-zA-Z0-9.-]"
#define HTTP_HEADER_PORT_MATCH ":%[0-9]"
#define HTTP_HEADER_URL_PATH_MATCH "%[/a-zA-Z0-9._-=@,]"

/*
************************************************************************
* GLOBAL VARIABLE/POINTER DEFINITIONS                                  *
************************************************************************
*/

/* misc variables */
int verbose = FALSE;

/* time thresholds to determine exit code */
int use_warning_interval = FALSE;
double warning_interval = 0;
int use_critical_interval = FALSE;
double critical_interval = 0;
double elapsed_time = 0;
struct timeval start_tv;

/* variables concerning the server host */
int use_server_hostname = FALSE;
char *server_hostname = "";  // hostname for use in HTTPs Host: header
char *server_host = "";      // hostname or ip address for tcp connect
int use_server_port = FALSE;
int server_port = HTTP_PORT;

int use_basic_auth = FALSE;
char basic_auth[MAX_INPUT_BUFFER] = "";

/* variables concerning server responce */
struct pageref {
        char   *content;
        size_t size;
        char   *status;
        char   *header;
        char   *body;
};

/* variables concerning ssl connections */
int use_ssl = FALSE;
#ifdef HAVE_SSL
int server_certificate_min_days_valid = 0;
int check_server_certificate = FALSE;
X509 *server_certificate;            // structure containing server certificate
int use_client_certificate = FALSE;
char *client_certificate_file = NULL;
int use_client_certificate_passphrase = FALSE;
char *client_certificate_passphrase = NULL;
int use_ca_certificate = FALSE;
char *ca_certificate_file = NULL;

BIO *bio_err = 0;             // error write context
#endif


/* variables concerning check behaviour */
char *http_method = DEFAULT_HTTP_METHOD;
char *http_url_path = "";
int use_http_post_data = FALSE;
char *http_post_data = "";
int use_min_content_length = FALSE;
int min_content_length = 0;
int use_http_expect_only = FALSE;
char http_expect[MAX_INPUT_BUFFER] = DEFAULT_HTTP_EXPECT;
int check_content_string = FALSE;
char content_string[MAX_INPUT_BUFFER] = "";
int http_redirect_state = DEFAULT_HTTP_REDIRECT_STATE;
int http_client_error_state = DEFAULT_HTTP_CLIENT_ERROR_STATE;

#ifdef HAVE_REGEX_H
regex_t regex_preg;
regmatch_t regex_pmatch[REGEX_REGS];
int check_content_regex = FALSE;
char content_regex[MAX_REGEX_SIZE] = "";
int regex_cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
int regex_error = 0;
char regex_error_buffer[MAX_INPUT_BUFFER] = "";
#endif



/*
************************************************************************
* FUNCTION PROTOTYPES                                                  *
************************************************************************
*/

void print_usage( void );
void print_help( void );
int process_arguments (int, char **);
int http_request( int sock, struct pageref *page);

int parse_http_response( struct pageref *page );
int check_http_response( struct pageref *page );
int check_http_content( struct pageref *page );
int prepare_follow_redirect( struct pageref *page );

static char *base64 (char *bin, int len);

#ifdef HAVE_SSL
int ssl_terminate( int state, char *string );
static int passwd_cb( char *buf, int num, int rwflag, void *userdata );
static void sigpipe_handle( int x );
SSL_CTX * initialize_ssl_ctx( void );
void destroy_ssl_ctx( SSL_CTX *ctx );
int fetch_server_certificate( SSL *ssl );
int check_server_certificate_chain( SSL *ssl );
int check_server_certificate_hostname( void );
int check_server_certificate_expires( void );
int https_request( SSL_CTX *ctx, SSL *ssl, struct pageref *page );
#endif

/*
************************************************************************
* IMPLEMENTATION                                                       * 
************************************************************************
*/

/*
 * main()
 *
 * PSEUDOCODE OF HOW MAIN IS SUPPOSED TO WORK
 *
 * process command line arguments including sanity check
 * initialize alarm signal handling
 * if use_ssl
 *   build ssl context
 * LOOP:
 * make tcp connection
 * if use_ssl 
 *   make ssl connection
 *   if use_server_hostname
 *     check if certificate matches hostname
 *   if check_server_certificate
 *     check expiration date of server certificate
 *     return STATUS
 *   else 
 *     request http page
 *     handle ssl rehandshake
 *   close ssl connection
 * else
 *   request http page
 * close tcp connection
 * analyze http page
 * if follow on redirect
 *   repeat LOOP
 * end of LOOP
 * destroy ssl context
 */
int
main (int argc, char **argv)
{
        int result = STATE_UNKNOWN;
        int sock;
        struct pageref page;
#ifdef HAVE_SSL
        SSL_CTX *ctx;
        SSL *ssl;
        BIO *sbio;
#endif

        if ( process_arguments(argc, argv) == ERROR )
                usage( "check_http: could not parse arguments\n" );

#ifdef HAVE_SSL
        /* build SSL context if required:
         * a) either we use ssl from the beginning OR
         * b) or we follor redirects wich may lead os to a ssl page 
         */
        if ( use_ssl || ( http_redirect_state == STATE_DEPENDENT ) )
                ctx=initialize_ssl_ctx();
#endif

        /* Loop around 3xx onredirect=follow */
        do {

                /*
                 * initialize alarm signal handling, set socket timeout, start timer
                 * socket_timeout and socket_timeout_alarm_handler are defined in
                 * netutils.c
                 */
                (void) signal( SIGALRM, socket_timeout_alarm_handler );
                (void) alarm( socket_timeout );
                gettimeofday( &start_tv, NULL );
        
                /* make a tcp connection */
                result = my_tcp_connect( server_host, server_port, &sock );
        
                /* result of tcp connect */
                if ( result == STATE_OK )
                {
#ifdef HAVE_SSL
                        /* make a ssl connection */
                        if ( use_ssl ) {
                                ssl=SSL_new( ctx );
                                sbio=BIO_new_socket( sock, BIO_NOCLOSE );
                                SSL_set_bio( ssl, sbio, sbio);
                                if ( SSL_connect( ssl ) <= 0 )
                                        ssl_terminate( STATE_CRITICAL, "check_http: SSL connect error" );

                                /* fetch server certificate */
                                result = fetch_server_certificate( ssl );

                                /* verify server certificate against CAs */
                                if ( ( result == STATE_OK ) && use_ca_certificate ) {
                                        result = check_server_certificate_chain( ssl );
                                }

                                /* check if certificate matches hostname */
                                if ( ( result == STATE_OK ) && use_server_hostname ) {
                                        result = check_server_certificate_hostname();
                                }

                                if ( result == STATE_OK ) {
                                        /* check server certificate expire date */
                                        if ( check_server_certificate ) {
                                                result = check_server_certificate_expires();
                                        /* OR: perform http request */
                                        } else {
                                                result = https_request( ctx, ssl, (struct pageref *) &page );
                                        }
                                }
                                SSL_shutdown( ssl );
                                SSL_free( ssl );
                        } else {
#endif
                        /* HTTP implementation */
                        result = http_request( sock, (struct pageref *) &page );
#ifdef HAVE_SSL
                        }
#endif
                        /* stop timer and calculate elapsed_time */
                        elapsed_time = delta_time( start_tv );

                        /* close the tcp connection */
                        close( sock );

                        /* reset the alarm */
                        alarm( 0 );

                        /* analyze http page */
                        /* TO DO */
                        if ( result == STATE_OK )
                                result = parse_http_response( (struct pageref *) &page );

                        if ( result == STATE_OK )
                                result = check_http_response( (struct pageref *) &page );

                        switch ( result ) {
                                case STATE_OK:
                                        /* weiter geht's */
                                        result = check_http_content( (struct pageref *) &page );
                                        break;
                                case STATE_DEPENDENT:
                                        /* try to determine redirect parameters */
                                        result = prepare_follow_redirect( (struct pageref *) &page );
                                        break;
                        }

                } else {
                        /* some error occured while trying to make a tcp connect */
                        exit( result );
                }

        } while ( result == STATE_DEPENDENT ); // end of onredirect loop

        /* destroy SSL context */
#ifdef HAVE_SSL
        if ( use_ssl || ( http_redirect_state == STATE_DEPENDENT ) )
                destroy_ssl_ctx( ctx );
#endif

        /* if we ever get to this point, everything went fine */
        printf( RESULT_TEMPLATE_STATUS_RESPONSE_TIME, 
                protocol_text( use_ssl ),
                state_text( result ),
                page.status,
                elapsed_time,
                elapsed_time );

        return result;
}


void
print_help( void )
{
        print_revision( progname, REVISION );
        printf
                ( "Copyright (c) %s %s <%s>\n\n%s\n",
                 COPYRIGHT, AUTHORS, EMAIL, HELP_TXT_SUMMARY );
        print_usage();
        printf( "NOTE: One or both of -H and -I must be specified\n" );
        printf( "\nOptions:\n" HELP_TXT_LONGOPTIONS "\n",
                HTTP_PORT, DEFAULT_HTTP_URL_PATH, HTTPS_PORT,
                DEFAULT_HTTP_EXPECT, DEFAULT_SOCKET_TIMEOUT );
#ifdef HAVE_SSL
        //printf( SSLDESCRIPTION );
#endif
}


void
print_usage( void )
{
        printf( "Usage:\n" " %s %s\n"
#ifdef HAVE_GETOPT_H
                " %s (-h | --help) for detailed help\n"
                " %s (-V | --version) for version information\n",
#else
                " %s -h for detailed help\n"
                " %s -V for version information\n",
#endif
        progname, HELP_TXT_OPTIONS, progname, progname );
}


/*
* process_arguments()
*
* process command line arguments either using getopt_long or getopt
* (parsing long argumants manually)
*/
int
process_arguments( int argc, char **argv )
{
        int c, i = 1;
        extern char *optarg;

#ifdef HAVE_GETOPT_H
        int option_index = 0;
        static struct option long_options[] = {
                STD_LONG_OPTS,
                {"file", required_argument, 0, 'F'},
                {"ip-address", required_argument, 0, 'I'},
                {"port", required_argument, 0, 'p'},
                {"url-path", required_argument, 0, 'u'},
                {"post-data", required_argument, 0, 'P'},
                {"ssl", no_argument, 0, 'S'},
                {"server-certificate-days", required_argument, 0, 'C'},
                {"basic-auth", required_argument, 0, 'a'},
                {"client-certificate", required_argument, 0, 'A'},
                {"passphrase", required_argument, 0, 'K'},
                {"ca-certificate", required_argument, 0, 'Z'},
                {"http-expect", required_argument, 0, 'e'},
                {"http-expect-only", required_argument, 0, 'E'},
                {"content-string", required_argument, 0, 's'},
                {"content-ereg-linespan", required_argument, 0, 'l'},
                {"content-ereg", required_argument, 0, 'r'},
                {"content-eregi", required_argument, 0, 'R'},
                {"onredirect", required_argument, 0, 'f'},
                {"onerror", required_argument, 0, 'g'},
                {"min", required_argument, 0, 'm'},
                {0, 0, 0, 0}
        };
#endif


        /* convert commonly used arguments to their equivalent standard options */
        for (c = 1; c < argc; c++) {
                if ( strcmp( "-to", argv[c]) == 0 )
                        strcpy( argv[c], "-t" );
                if ( strcmp( "-hn", argv[c]) == 0 )
                        strcpy( argv[c], "-H" );
                if ( strcmp( "-wt", argv[c]) == 0 )
                        strcpy( argv[c], "-w" );
                if ( strcmp( "-ct", argv[c]) == 0 )
                        strcpy( argv[c], "-c" );
        }

#define OPTCHARS "Vvht:c:w:H:F:I:p:u:P:SC:a:A:K:Z:e:E:s:r:R:f:g:lm:"


        while (1) {

#ifdef HAVE_GETOPT_H
                c = getopt_long( argc, argv, OPTCHARS, long_options, &option_index );
#else
                c = getopt( argc, argv, OPTCHARS );
#endif

                if ( ( c == -1 ) || ( c == EOF ) ) {
                        break;
                }

                switch (c) {
                case '?': /* usage */
                        usage2( "unknown argument", optarg );
                        break;

		/* Standard options */
                case 'h': /* help */
                        print_help();
                        exit( STATE_OK );
                        break;
                case 'V': /* version */
                        print_revision( progname, REVISION );
                        exit( STATE_OK );
                        break;
                case 'v': /* verbose */
                        verbose = TRUE;
                        break;
                case 't': /* timeout period */
                        if ( !is_intnonneg( optarg ) )
                                usage2( "timeout interval must be a non-negative integer", optarg );
                        /* socket_timeout is defined in netutils.h and defaults to
                         * DEFAULT_SOCKET_TIMEOUT from common.h
                         */
                        socket_timeout = atoi( optarg );
                        break;
                case 'c': /* critical time threshold */
                        if ( !is_nonnegative( optarg ) )
                                usage2( "invalid critical threshold", optarg );
                        critical_interval = strtod( optarg, NULL );
                        use_critical_interval = TRUE;
                        break;
                case 'w': /* warning time threshold */
                        if ( !is_nonnegative( optarg ) )
                                usage2( "invalid warning threshold", optarg );
                        warning_interval = strtod( optarg, NULL );
                        use_warning_interval = TRUE;
                        break;
                case 'H': /* Host Name (virtual host) */
                        /* this rejects FQDNs, so we leave it for now... 
                         *if ( !is_hostname( optarg ) )
                         *        usage2( "invalid hostname", optarg );
                         */
                        xasprintf( &server_hostname, "%s", optarg );
                        use_server_hostname = TRUE;
                        break;
                case 'F': /* File (dummy) */
                        break;
		/* End of standard options */


                case 'I': /* Server IP-address or Hostname */
                        /* this rejects FQDNs, so we leave it for now... 
                         *if ( !is_host( optarg ) )
                         *        usage2( "invalid ip address or hostname", optarg )
                         */
                        xasprintf( &server_host, "%s", optarg );
                        break;
                case 'p': /* Server port */
                        if ( !is_intnonneg( optarg ) )
                                usage2( "invalid port number", optarg );
                        server_port = atoi( optarg );
                        use_server_port = TRUE;
                        break;
                case 'S': /* use SSL */
#ifdef HAVE_SSL
                        use_ssl = TRUE;
                        if ( use_server_port == FALSE )
                                server_port = HTTPS_PORT;
#else
                        usage( "check_http: invalid option - SSL is not available\n" );
#endif
                        break;
                case 'C': /* Server certificate warning time threshold */
#ifdef HAVE_SSL
                        if ( !is_intnonneg( optarg ) )
                                usage2( "invalid certificate expiration period", optarg );
                        server_certificate_min_days_valid = atoi( optarg );
                        check_server_certificate = TRUE;
#else
                        usage( "check_http: invalid option - SSL is not available\n" );
#endif
                        break;
                case 'a': /* basic authorization info */
                        strncpy( basic_auth, optarg, MAX_INPUT_BUFFER - 1 );
                        basic_auth[MAX_INPUT_BUFFER - 1] = 0;
                        use_basic_auth = TRUE;
                        break;
                case 'A': /* client certificate */
#ifdef HAVE_SSL
                        xasprintf( &client_certificate_file, "%s", optarg );
                        use_client_certificate = TRUE;
#else
                        usage( "check_http: invalid option - SSL is not available\n" );
#endif
                        break;
                case 'K': /* client certificate passphrase */
#ifdef HAVE_SSL
                        xasprintf( &client_certificate_passphrase, "%s", optarg );
                        use_client_certificate_passphrase = TRUE;
#else
                        usage( "check_http: invalid option - SSL is not available\n" );
#endif
                case 'Z': /* valid CA certificates */
#ifdef HAVE_SSL
                        xasprintf( &ca_certificate_file, "%s", optarg );
                        use_ca_certificate = TRUE;
#else
                        usage( "check_http: invalid option - SSL is not available\n" );
#endif
                        break;
                case 'u': /* URL PATH */
                        xasprintf( &http_url_path, "%s", optarg );
                        break;
                case 'P': /* POST DATA */
                        xasprintf( &http_post_data, "%s", optarg );
                        use_http_post_data = TRUE;
                        xasprintf( &http_method, "%s", "POST" );
                        break;
                case 'e': /* expected string in first line of HTTP response */
                        strncpy( http_expect , optarg, MAX_INPUT_BUFFER - 1 );
                        http_expect[MAX_INPUT_BUFFER - 1] = 0;
                        break;
                case 'E': /* expected string in first line of HTTP response and process no other check*/
                        strncpy( http_expect , optarg, MAX_INPUT_BUFFER - 1 );
                        http_expect[MAX_INPUT_BUFFER - 1] = 0;
                        use_http_expect_only = TRUE;
                        break;
                case 's': /* expected (sub-)string in content */
                        strncpy( content_string , optarg, MAX_INPUT_BUFFER - 1 );
                        content_string[MAX_INPUT_BUFFER - 1] = 0;
                        check_content_string = TRUE;
                        break;
                case 'l': /* regex linespan */
#ifdef HAVE_REGEX_H
                        regex_cflags &= ~REG_NEWLINE;
#else
                        usage( "check_http: call for regex which was not a compiled option\n" );
#endif
                        break;
                case 'R': /* expected case insensitive regular expression in content */
#ifdef HAVE_REGEX_H
                        regex_cflags |= REG_ICASE;
#else
                        usage( "check_http: call for regex which was not a compiled option\n" );
#endif
                case 'r': /* expected regular expression in content */
#ifdef HAVE_REGEX_H
                        strncpy( content_regex , optarg, MAX_REGEX_SIZE - 1 );
                        content_regex[MAX_REGEX_SIZE - 1] = 0;
                        check_content_regex = TRUE;
                        regex_error = regcomp( &regex_preg, content_regex, regex_cflags );
                        if ( regex_error != 0 ) {
                                regerror( regex_error, &regex_preg, regex_error_buffer, MAX_INPUT_BUFFER );
                                printf( "Could Not Compile Regular Expression: %s", regex_error_buffer );
                                return ERROR;
                        }
#else
                        usage( "check_http: call for regex which was not a compiled option\n" );
#endif
                        break;
                case 'f': /* onredirect (3xx errors) */
                        if ( !strcmp( optarg, "follow" ) )
                                http_redirect_state = STATE_DEPENDENT;
                        if ( !strcmp( optarg, "unknown" ) )
                                http_redirect_state = STATE_UNKNOWN;
                        if ( !strcmp( optarg, "ok" ) )
                                http_redirect_state = STATE_OK;
                        if ( !strcmp( optarg, "warning" ) )
                                http_redirect_state = STATE_WARNING;
                        if ( !strcmp( optarg, "critical" ) )
                                http_redirect_state = STATE_CRITICAL;
                        break;
                case 'g': /* onerror (4xx errors) */
                        if ( !strcmp( optarg, "unknown" ) )
                                http_client_error_state = STATE_UNKNOWN;
                        if ( !strcmp( optarg, "ok" ) )
                                http_client_error_state = STATE_OK;
                        if ( !strcmp( optarg, "warning" ) )
                                http_client_error_state = STATE_WARNING;
                        if ( !strcmp( optarg, "critical" ) )
                                http_client_error_state = STATE_CRITICAL;
                        break;
                case 'm': /* min */
                        if ( !is_intnonneg( optarg ) )
                                usage2( "invalid page size", optarg );
                        min_content_length = atoi( optarg );
                        use_min_content_length = TRUE;
                        break;
                } // end switch 
        } // end while(1)

        c = optind;


        /* Sanity checks on supplied command line arguments */

        /* 1. if both host and hostname are not defined, try to
         *    fetch one more argument which is possibly supplied
         *    without an option
         */
        if ( ( strcmp( server_host, "" ) ) && (c < argc) ) {
                xasprintf( &server_host, "%s", argv[c++] );
        }

        /* 2. check if another artument is supplied
         */
        if ( ( strcmp( server_hostname, "" ) == 0 ) && (c < argc) ) {
               xasprintf( &server_hostname, "%s", argv[c++] ); 
        }

        /* 3. if host is still not defined, just copy hostname, 
         *    which is then guaranteed to be defined by now
         */
        if ( strcmp( server_host, "") == 0 ) {
                if ( strcmp( server_hostname, "" ) == 0 ) {
			usage ("check_http: you must specify a server address or host name\n");
                } else {
                        xasprintf( &server_host, "%s", server_hostname );
                }
        }

        /* 4. check if content checks for a string and a regex
         *    are requested for only one of both is possible at
         *    a time
         */
        if ( check_content_string && check_content_regex )
                usage( "check_http: you can only check for string OR regex at a time\n" );

        /* 5. check for options which require use_ssl */
        if ( check_server_certificate && !use_ssl ) 
                usage( "check_http: you must use -S to check server certificate\n" );
        if ( use_client_certificate && !use_ssl ) 
                usage( "check_http: you must use -S to authenticate with a client certificate\n" );
        if ( use_ca_certificate && !use_ssl ) 
                usage( "check_http: you must use -S to check server certificate against CA certificates\n" );

        /* 6. check for passphrase without client certificate */
        if ( use_client_certificate_passphrase && !use_client_certificate )
                usage( "check_http: you must supply a client certificate to use a passphrase\n" );


        /* Finally set some default values if necessary */
        if ( strcmp( http_method, "" ) == 0 )
                xasprintf( &http_method, "%s", DEFAULT_HTTP_METHOD );
        if ( strcmp( http_url_path, "" ) == 0 ) {
                xasprintf( &http_url_path, "%s", DEFAULT_HTTP_URL_PATH );
        }

        return TRUE;
}


int 
http_request( int sock, struct pageref *page )
{
        char *buffer = "";
        char recvbuff[MAX_INPUT_BUFFER] = ""; 
        int buffer_len = 0;
        int content_len = 0;
        size_t sendsize = 0;
        size_t recvsize = 0;
        char *content = ""; 
        size_t size = 0;
        char *basic_auth_encoded = NULL;

        xasprintf( &buffer, HTTP_TEMPLATE_REQUEST, buffer, http_method, http_url_path );

        xasprintf( &buffer, HTTP_TEMPLATE_HEADER_USERAGENT, buffer, progname, REVISION, PACKAGE_VERSION );

        if ( use_server_hostname ) {
                xasprintf( &buffer, HTTP_TEMPLATE_HEADER_HOST, buffer, server_hostname );
        }

        if ( use_basic_auth ) {
                basic_auth_encoded = base64( basic_auth, strlen( basic_auth ) );
                xasprintf( &buffer, HTTP_TEMPLATE_HEADER_AUTH, buffer, basic_auth_encoded );
        }

        /* either send http POST data */
        if ( use_http_post_data ) {
        /* based on code written by Chris Henesy <lurker@shadowtech.org> */
                xasprintf( &buffer, "Content-Type: application/x-www-form-urlencoded\r\n" );
                xasprintf( &buffer, "Content-Length: %i\r\n\r\n", buffer, content_len );
                xasprintf( &buffer, "%s%s%s", buffer, http_post_data, "\r\n" );
                sendsize = send( sock, buffer, strlen( buffer ), 0 );
                if ( sendsize < strlen( buffer ) ) {
                        printf( "ERROR: Incomplete write\n" );
                        return STATE_CRITICAL;
                }
        /* or just a newline */
        } else {
	        xasprintf( &buffer, "%s%s", buffer, "\r\n" );
	       	sendsize = send( sock, buffer, strlen( buffer ) , 0 );
	       	if ( sendsize < strlen( buffer ) ) {
	  	        printf( "ERROR: Incomplete write\n" );
	                return STATE_CRITICAL;
	        }
	}


        /* read server's response */

        do {
                recvsize = recv( sock, recvbuff, MAX_INPUT_BUFFER - 1, 0 );
                if ( recvsize > (size_t) 0 ) {
                        recvbuff[recvsize] = '\0';
                        xasprintf( &content, "%s%s", content, recvbuff );
                        size += recvsize;
                }
        } while ( recvsize > (size_t) 0 );

        xasprintf( &page->content, "%s", content );
        page->size = size;

        /* return a CRITICAL status if we couldn't read any data */
        if ( size == (size_t) 0)
                ssl_terminate( STATE_CRITICAL, "No data received" );

        return STATE_OK;
}


int
parse_http_response( struct pageref *page )
{
	char *content = "";     //local copy of struct member
        char *status = "";      //local copy of struct member
        char *header = "";      //local copy of struct member
        size_t len = 0;         //temporary used
        char *pos = "";         //temporary used

        xasprintf( &content, "%s", page->content );

        /* find status line and null-terminate it */
        // copy content to status
        status = content;
        
        // find end of status line and copy pointer to pos
        content += (size_t) strcspn( content, "\r\n" );
        pos = content;

        // advance content pointer behind the newline of status line
        content += (size_t) strspn( content, "\r\n" );

        // null-terminate status line at pos
        status[strcspn( status, "\r\n")] = 0;
        strip( status );

        // copy final status to struct member
        page->status = status;


        /* find header and null-terminate it */
        // copy remaining content to header
        header = content;

        // loop until line containing only newline is found (end of header)
        while ( strcspn( content, "\r\n" ) > 0 ) {
                //find end of line and copy pointer to pos
                content += (size_t) strcspn( content, "\r\n" );
                pos = content;

                if ( ( strspn( content, "\r" ) == 1 && strspn( content, "\r\n" ) >= 2 ) ||
                     ( strspn( content, "\n" ) == 1 && strspn( content, "\r\n" ) >= 2 ) )
                        content += (size_t) 2;
                else
                        content += (size_t) 1;
        }
        // advance content pointer behind the newline
        content += (size_t) strspn( content, "\r\n" );

        // null-terminate header at pos
        header[pos - header] = 0;

        // copy final header to struct member
        page->header = header;


        // copy remaining content to body
        page->body = content;

        if ( verbose ) {
                printf( "STATUS: %s\n", page->status );
                printf( "HEADER: \n%s\n", page->header );
                printf( "BODY: \n%s\n", page->body );
        }

        return STATE_OK;
}


int
check_http_response( struct pageref *page )
{
        char *msg = "";

        /* check response time befor anything else */
        if ( use_critical_interval && ( elapsed_time > critical_interval ) ) {
                xasprintf( &msg, RESULT_TEMPLATE_RESPONSE_TIME, 
                                protocol_text( use_ssl ),
                                state_text( STATE_CRITICAL ),
                                elapsed_time,
                                elapsed_time ); 
                terminate( STATE_CRITICAL, msg );
        }
        if ( use_warning_interval  && ( elapsed_time > warning_interval ) ) {
                xasprintf( &msg, RESULT_TEMPLATE_RESPONSE_TIME, 
                                protocol_text( use_ssl ),
                                state_text( STATE_WARNING ),
                                elapsed_time,
                                elapsed_time );
                terminate( STATE_WARNING, msg );
        }


        /* make sure the status line matches the response we are looking for */
        if ( strstr( page->status, http_expect ) ) {
                /* The result is only checked against the expected HTTP status line,
                   so exit immediately after this check */
                if ( use_http_expect_only ) {
                        if ( ( server_port == HTTP_PORT ) 
#ifdef HAVE_SSL
                             || ( server_port == HTTPS_PORT ) )
#else
                           )
#endif 
                                xasprintf( &msg, "Expected HTTP response received from host\n" );
                        else
                                xasprintf( &msg, "Expected HTTP response received from host on port %d\n", server_port );
                        terminate( STATE_OK, msg );
                }
        } else {
                if ( ( server_port == HTTP_PORT )
#ifdef HAVE_SSL
                     || ( server_port == HTTPS_PORT ) )
#else
                   )
#endif
                        xasprintf( &msg, "Invalid HTTP response received from host\n" );
                else
                        xasprintf( &msg, "Invalid HTTP response received from host on port %d\n", server_port );
                terminate( STATE_CRITICAL, msg );
        }

        /* check the return code */
        /* server errors result in a critical state */
        if ( strstr( page->status, "500" ) ||
             strstr( page->status, "501" ) ||
             strstr( page->status, "502" ) ||
             strstr( page->status, "503" ) ||
             strstr( page->status, "504" ) ||
             strstr( page->status, "505" )) {
                xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                protocol_text( use_ssl ),
                                state_text( http_client_error_state ),
                                page->status,
                                elapsed_time,
                                elapsed_time );
                terminate( STATE_CRITICAL, msg );
        }

        /* client errors result in a warning state */
        if ( strstr( page->status, "400" ) ||
             strstr( page->status, "401" ) ||
             strstr( page->status, "402" ) ||
             strstr( page->status, "403" ) ||
             strstr( page->status, "404" ) ||
             strstr( page->status, "405" ) ||
             strstr( page->status, "406" ) ||
             strstr( page->status, "407" ) ||
             strstr( page->status, "408" ) ||
             strstr( page->status, "409" ) ||
             strstr( page->status, "410" ) ||
             strstr( page->status, "411" ) ||
             strstr( page->status, "412" ) ||
             strstr( page->status, "413" ) ||
             strstr( page->status, "414" ) ||
             strstr( page->status, "415" ) ||
             strstr( page->status, "416" ) ||
             strstr( page->status, "417" ) ) {
                xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                protocol_text( use_ssl ),
                                state_text( http_client_error_state ),
                                page->status,
                                elapsed_time,
                                elapsed_time );
                terminate( http_client_error_state, msg );
        }

        /* check redirected page if specified */
        if (strstr( page->status, "300" ) ||
            strstr( page->status, "301" ) ||
            strstr( page->status, "302" ) ||
            strstr( page->status, "303" ) ||
            strstr( page->status, "304" ) ||
            strstr( page->status, "305" ) ||
            strstr( page->status, "306" ) ||
            strstr( page->status, "307" ) ) {
                if ( http_redirect_state == STATE_DEPENDENT ) {
                                /* returning STATE_DEPENDENT means follow redirect */
                                return STATE_DEPENDENT;
                } else {
                        xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                        protocol_text( use_ssl ),
                                        state_text( http_redirect_state ),
                                        page->status,
                                        elapsed_time,
                                        elapsed_time );
                        terminate( http_redirect_state, msg );
                }
        }

        return STATE_OK;
}

int
check_http_content( struct pageref *page )
{
        char *msg = "";

        /* check for string in content */
        if ( check_content_string ) {
                if ( strstr( page->content, content_string ) ) {
                        xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                        protocol_text( use_ssl ),
                                        state_text( STATE_OK ),
                                        page->status,
                                        elapsed_time,
                                        elapsed_time );
                        terminate( STATE_OK, msg );
                } else {
                        xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                        protocol_text( use_ssl ),
                                        state_text( STATE_CRITICAL ),
                                        page->status,
                                        elapsed_time,
                                        elapsed_time );
                        terminate( STATE_CRITICAL, msg );
                }
        }

#ifdef HAVE_REGEX_H
        /* check for regex in content */
        if ( check_content_regex ) {
                regex_error = regexec( &regex_preg, page->content, REGEX_REGS, regex_pmatch, 0);
                if ( regex_error == 0 ) {
                        xasprintf( &msg, RESULT_TEMPLATE_STATUS_RESPONSE_TIME,
                                        protocol_text( use_ssl ),
                                        state_text( STATE_OK ),
                                        page->status,
                                        elapsed_time,
                                        elapsed_time );
                        terminate( STATE_OK, msg );
                } else {
                        if ( regex_error == REG_NOMATCH ) {
                                xasprintf( &msg, "%s, %s: regex pattern not found\n",
                                                protocol_text( use_ssl) ,
                                                state_text( STATE_CRITICAL ) );
                                terminate( STATE_CRITICAL, msg );
                        } else {
                                regerror( regex_error, &regex_preg, regex_error_buffer, MAX_INPUT_BUFFER);
                                xasprintf( &msg, "%s %s: Regex execute Error: %s\n",
                                                protocol_text( use_ssl) ,
                                                state_text( STATE_CRITICAL ),
                                                regex_error_buffer );
                                terminate( STATE_CRITICAL, msg );
                        }
                }
        }
#endif

        return STATE_OK;
}


int
prepare_follow_redirect( struct pageref *page )
{
        char *header = NULL;
        char *msg = "";
        char protocol[6];
        char hostname[MAX_IPV4_HOSTLENGTH];
        char port[6];
        char *url_path = NULL;
        char *orig_url_path = NULL;
        char *orig_url_dirname = NULL;
        size_t len = 0;

        xasprintf( &header, "%s", page->header );


        /* restore some default values */
        use_http_post_data = FALSE;
        xasprintf( &http_method, "%s", DEFAULT_HTTP_METHOD );

        /* copy url of original request, maybe we need it to compose 
           absolute url from relative Location: header */
        xasprintf( &orig_url_path, "%s", http_url_path );

        while ( strcspn( header, "\r\n" ) > (size_t) 0 ) {
                url_path = realloc( url_path, (size_t) strcspn( header, "\r\n" ) );
                if ( url_path == NULL )
                        terminate( STATE_UNKNOWN, "HTTP UNKNOWN: could not reallocate url_path" );


                /* Try to find a Location header combination of METHOD HOSTNAME PORT and PATH */
                /* 1. scan for Location: http[s]://hostname:port/path */
                if ( sscanf ( header, HTTP_HEADER_LOCATION_MATCH HTTP_HEADER_PROTOCOL_MATCH HTTP_HEADER_HOSTNAME_MATCH HTTP_HEADER_PORT_MATCH HTTP_HEADER_URL_PATH_MATCH, &protocol, &hostname, &port, url_path ) == 4 ) {
                        xasprintf( &server_hostname, "%s", hostname );
                        xasprintf( &server_host, "%s", hostname );
                        use_ssl = chk_protocol(protocol);
                        server_port = atoi( port );
                        xasprintf( &http_url_path, "%s", url_path );
                        return STATE_DEPENDENT;
                } 
                else if ( sscanf ( header, HTTP_HEADER_LOCATION_MATCH HTTP_HEADER_PROTOCOL_MATCH HTTP_HEADER_HOSTNAME_MATCH HTTP_HEADER_URL_PATH_MATCH, &protocol, &hostname, url_path ) == 3) {
                        xasprintf( &server_hostname, "%s", hostname );
                        xasprintf( &server_host, "%s", hostname );
                        use_ssl = chk_protocol(protocol);
                        server_port = protocol_std_port(use_ssl);
                        xasprintf( &http_url_path, "%s", url_path );
                        return STATE_DEPENDENT;
                }
                else if ( sscanf ( header, HTTP_HEADER_LOCATION_MATCH HTTP_HEADER_PROTOCOL_MATCH HTTP_HEADER_HOSTNAME_MATCH HTTP_HEADER_PORT_MATCH, &protocol, &hostname, &port ) == 3) {
                        xasprintf( &server_hostname, "%s", hostname );
                        xasprintf( &server_host, "%s", hostname );
                        use_ssl = chk_protocol(protocol);
                        server_port = atoi( port );
                        xasprintf( &http_url_path, "%s", DEFAULT_HTTP_URL_PATH );
                        return STATE_DEPENDENT;
                }
                else if ( sscanf ( header, HTTP_HEADER_LOCATION_MATCH HTTP_HEADER_PROTOCOL_MATCH HTTP_HEADER_HOSTNAME_MATCH, protocol, hostname ) == 2 ) {
                        xasprintf( &server_hostname, "%s", hostname );
                        xasprintf( &server_host, "%s", hostname );
                        use_ssl = chk_protocol(protocol);
                        server_port = protocol_std_port(use_ssl);
                        xasprintf( &http_url_path, "%s", DEFAULT_HTTP_URL_PATH );
                }
                else if ( sscanf ( header, HTTP_HEADER_LOCATION_MATCH HTTP_HEADER_URL_PATH_MATCH, url_path ) == 1 ) {
                        /* check for relative url and prepend path if necessary */
                        if ( ( url_path[0] != '/' ) && ( orig_url_dirname = strrchr( orig_url_path, '/' ) ) ) {
                                *orig_url_dirname = '\0';
                                xasprintf( &http_url_path, "%s%s", orig_url_path, url_path );
                        } else {
                                xasprintf( &http_url_path, "%s", url_path );
                        }
                        return STATE_DEPENDENT;
                }
                header += (size_t) strcspn( header, "\r\n" );
                header += (size_t) strspn( header, "\r\n" );
        } /* end while (header) */
        

        /* default return value is STATE_DEPENDENT to continue looping in main() */
        xasprintf( &msg, "% %: % - Could not find redirect Location",
                        protocol_text( use_ssl ),
                        state_text( STATE_UNKNOWN ),
                        page->status );
        terminate( STATE_UNKNOWN, msg );
}

#ifdef HAVE_SSL
int
https_request( SSL_CTX *ctx, SSL *ssl, struct pageref *page )
{
        char *buffer = "";
        char recvbuff[MAX_INPUT_BUFFER] = "";
        int buffer_len = 0;
        int content_len = 0;
        size_t sendsize = 0;
        size_t recvsize = 0;
        char *content = "";
        size_t size = 0;
        char *basic_auth_encoded = NULL;

        xasprintf( &buffer, HTTP_TEMPLATE_REQUEST, buffer, http_method, http_url_path );

        xasprintf( &buffer, HTTP_TEMPLATE_HEADER_USERAGENT, buffer, progname, REVISION, PACKAGE_VERSION );
        
        if ( use_server_hostname ) {
                xasprintf( &buffer, HTTP_TEMPLATE_HEADER_HOST, buffer, server_hostname );
        }

        if ( use_basic_auth ) {
                basic_auth_encoded = base64( basic_auth, strlen( basic_auth ) );
                xasprintf( &buffer, HTTP_TEMPLATE_HEADER_AUTH, buffer, basic_auth_encoded );
        }

        /* either send http POST data */
        if ( use_http_post_data ) {
                xasprintf( &buffer, "%sContent-Type: application/x-www-form-urlencoded\r\n", buffer );
                xasprintf( &buffer, "%sContent-Length: %i\r\n\r\n", buffer, content_len );
                xasprintf( &buffer, "%s%s%s", buffer, http_post_data, "\r\n" );
                sendsize = SSL_write( ssl, buffer, strlen( buffer ) );
                switch ( SSL_get_error( ssl, sendsize ) ) {
                        case SSL_ERROR_NONE:
                                if ( sendsize < strlen( buffer ) )
                                        ssl_terminate( STATE_CRITICAL, "SSL ERROR: Incomplete write.\n" );
                                break;
                        default:
                                ssl_terminate( STATE_CRITICAL, "SSL ERROR: Write problem.\n" );
                                break;
                }
        /* or just a newline */
        } else {

                xasprintf( &buffer, "%s\r\n", buffer );
                sendsize = SSL_write( ssl, buffer, strlen( buffer ) );
                switch ( SSL_get_error( ssl, sendsize ) ) {
                        case SSL_ERROR_NONE:
                                if ( sendsize < strlen( buffer ) )
                                        ssl_terminate( STATE_CRITICAL, "SSL ERROR: Incomplete write.\n" );
                                break;
                        default:
                                ssl_terminate( STATE_CRITICAL, "SSL ERROR: Write problem.\n" );
                                break;
                }
        }


        /* read server's response */

        do {
                recvsize = SSL_read( ssl, recvbuff, MAX_INPUT_BUFFER - 1 );

                switch ( SSL_get_error( ssl, recvsize ) ) {
                        case SSL_ERROR_NONE:
                                if ( recvsize > (size_t) 0 ) {
                                        recvbuff[recvsize] = '\0';
                                        xasprintf( &content, "%s%s", content, recvbuff );
                                        size += recvsize;
                                }
                                break;
                        case SSL_ERROR_WANT_READ:
                                if ( use_client_certificate ) {
                                        continue;
                                } else {
                                        // workaround while we don't have anonymous client certificates: return OK
                                        //ssl_terminate( STATE_WARNING, "HTTPS WARNING - Client Certificate required.\n" );
                                        ssl_terminate( STATE_OK, "HTTPS WARNING - Client Certificate required.\n" );
                                }
                                break;
                        case SSL_ERROR_ZERO_RETURN:
                                break;
                        case SSL_ERROR_SYSCALL:
                                ssl_terminate( STATE_CRITICAL, "SSL ERROR: Premature close.\n" );
                                break;
                        default:
                                ssl_terminate( STATE_CRITICAL, "SSL ERROR: Read problem.\n" );
                                break;
                }
        } while ( recvsize > (size_t) 0 );

        xasprintf( &page->content, "%s", content );
        page->size = size;

        /* return a CRITICAL status if we couldn't read any data */
        if ( size == (size_t) 0)
                ssl_terminate( STATE_CRITICAL, "No data received" );

        return STATE_OK;
}
#endif


#ifdef HAVE_SSL
int 
ssl_terminate(int state, char *string ) {
        ERR_print_errors( bio_err );
        terminate( state, string );
}
#endif

#ifdef HAVE_SSL
static int 
password_cb( char *buf, int num, int rwflag, void *userdata )
{
        if ( num < strlen( client_certificate_passphrase ) + 1 )
                return( 0 );

        strcpy( buf, client_certificate_passphrase );
        return( strlen( client_certificate_passphrase ) );
}
#endif

#ifdef HAVE_SSL
static void 
sigpipe_handle( int x ) {
}
#endif

#ifdef HAVE_SSL
SSL_CTX *
initialize_ssl_ctx( void )
{
        SSL_METHOD *meth;
        SSL_CTX *ctx;

        if ( !bio_err ) {
                /* Global system initialization */
                SSL_library_init();
                SSL_load_error_strings();

                /* An error write context */
                bio_err=BIO_new_fp( stderr, BIO_NOCLOSE );
        }

        /* set up as SIGPIPE handler */
        signal( SIGPIPE, sigpipe_handle );

        /* create our context */
        meth=SSLv3_method();
        ctx=SSL_CTX_new( meth );

        /* load client certificate and key */
        if ( use_client_certificate ) {
                if ( !(SSL_CTX_use_certificate_chain_file( ctx, client_certificate_file )) ) 
                        ssl_terminate( STATE_CRITICAL, "check_http: can't read client certificate file" );

                /* set client certificate key passphrase */
                if ( use_client_certificate_passphrase ) {
                        SSL_CTX_set_default_passwd_cb( ctx, password_cb );
                }

                if ( !(SSL_CTX_use_PrivateKey_file( ctx, client_certificate_file, SSL_FILETYPE_PEM )) )
                        ssl_terminate( STATE_CRITICAL, "check_http: can't read client certificate key file" );
        }

        /* load the CAs we trust */
        if ( use_ca_certificate ) {
                if ( !(SSL_CTX_load_verify_locations( ctx, ca_certificate_file, 0 )) )
                        ssl_terminate( STATE_CRITICAL, "check_http: can't read CA certificate file" );

#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
                SSL_CTX_set_verify_depth( ctx, 1 );
#endif
        }

        return ctx;
}
#endif

#ifdef HAVE_SSL
void destroy_ssl_ctx( SSL_CTX *ctx )
{
        SSL_CTX_free( ctx );
}
#endif

#ifdef HAVE_SSL
int
fetch_server_certificate( SSL *ssl )
{
        server_certificate = SSL_get_peer_certificate( ssl );
        if ( server_certificate == NULL )
                ssl_terminate( STATE_CRITICAL,  "SSL ERROR: Cannot retrieve server certificate.\n" );

        return STATE_OK;
}
#endif


#ifdef HAVE_SSL
int
check_server_certificate_chain( SSL *ssl )
{
        if ( SSL_get_verify_result( ssl ) != X509_V_OK )
                ssl_terminate( STATE_CRITICAL, "SSL ERROR: Cannot verify server certificate chain.\n" );

        return STATE_OK;
}
#endif


#ifdef HAVE_SSL
int
check_server_certificate_hostname( )
{
        char server_CN[256];
        char *msg = NULL;
        X509_NAME_get_text_by_NID( X509_get_subject_name( server_certificate ), NID_commonName, server_CN, 256 );
        if ( strcasecmp( server_CN, server_hostname ) ) {
                xasprintf( &msg, "SSL ERROR: Server Certificate does not match Hostname %s.\n", server_hostname );
                ssl_terminate( STATE_WARNING, msg );
        }

        return STATE_OK;
}
#endif

#ifdef HAVE_SSL
int
check_server_certificate_expires( )
{
        ASN1_STRING *tm;
        int offset;
        struct tm stamp;
        int days_left;
        char timestamp[17] = "";
        char *msg = NULL;

        /* Retrieve timestamp of certificate */
        tm = X509_get_notAfter( server_certificate );

        /* Generate tm structure to process timestamp */
        if ( tm->type == V_ASN1_UTCTIME ) {
                if ( tm->length < 10 ) {
                        ssl_terminate( STATE_CRITICAL, "ERROR: Wrong time format in certificate.\n" );
                } else {
                        stamp.tm_year = ( tm->data[0] - '0' ) * 10 + ( tm->data[1] - '0' );
                        if ( stamp.tm_year < 50 )
                                stamp.tm_year += 100;
                        offset = 0;
                }
        } else {
                if ( tm->length < 12 ) {
                        ssl_terminate( STATE_CRITICAL, "ERROR: Wrong time format in certificate.\n" );
                } else {
                        stamp.tm_year =
                                ( tm->data[0] - '0' ) * 1000 + ( tm->data[1] - '0' ) * 100 +
                                ( tm->data[2] - '0' ) * 10 + ( tm->data[3] - '0' );
                        stamp.tm_year -= 1900;
                        offset = 2;
                }
        }
        stamp.tm_mon =
                ( tm->data[2 + offset] - '0' ) * 10 + ( tm->data[3 + offset] - '0' ) - 1;
        stamp.tm_mday =
                ( tm->data[4 + offset] - '0' ) * 10 + ( tm->data[5 + offset] - '0' );
        stamp.tm_hour =
                ( tm->data[6 + offset] - '0' ) * 10 + ( tm->data[7 + offset] - '0' );
        stamp.tm_min =
                ( tm->data[8 + offset] - '0' ) * 10 + ( tm->data[9 + offset] - '0' );
        stamp.tm_sec = 0;
        stamp.tm_isdst = -1;

        days_left = ( mktime( &stamp ) - time( NULL ) ) / 86400;
        snprintf
                ( timestamp, 17, "%02d.%02d.%04d %02d:%02d",
                 stamp.tm_mday, stamp.tm_mon +1, stamp.tm_year + 1900, 
                 stamp.tm_hour, stamp.tm_min );

        if ( ( days_left > 0 ) && ( days_left <= server_certificate_min_days_valid ) ) {
                xasprintf( &msg, "Certificate expires in %d day(s) (%s).\n", days_left, timestamp );
                ssl_terminate( STATE_WARNING, msg );
        }
        if ( days_left < 0 ) {
                xasprintf( &msg, "Certificate expired on %s.\n", timestamp );
                ssl_terminate( STATE_CRITICAL, msg );
        }

        if (days_left == 0) {
                xasprintf( &msg, "Certificate expires today (%s).\n", timestamp );
                ssl_terminate( STATE_WARNING, msg );
        }

        xasprintf( &msg, "Certificate will expire on %s.\n", timestamp );
        ssl_terminate( STATE_OK, msg );
}
#endif

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

