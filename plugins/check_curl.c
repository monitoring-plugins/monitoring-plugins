/*****************************************************************************
 *
 * Monitoring check_curl plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
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
const char *copyright = "2006-2024";
const char *email = "devel@monitoring-plugins.org";

#include "check_curl.d/config.h"
#include "states.h"
#include "thresholds.h"
#include <stdbool.h>
#include <ctype.h>

#include "common.h"
#include "utils.h"

#ifndef LIBCURL_PROTOCOL_HTTP
#	error libcurl compiled without HTTP support, compiling check_curl plugin does not makes a lot of sense
#endif

#include "curl/curl.h"
#include "curl/easy.h"

#include "picohttpparser.h"

#include "uriparser/Uri.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
#	include <openssl/opensslv.h>
#endif

#include <netdb.h>

#define MAKE_LIBCURL_VERSION(major, minor, patch) ((major) * 0x10000 + (minor) * 0x100 + (patch))

#define INET_ADDR_MAX_SIZE INET6_ADDRSTRLEN
enum {
	MAX_IPV4_HOSTLENGTH = 255,
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

/* to know the underlying SSL library used by libcurl */
typedef enum curlhelp_ssl_library {
	CURLHELP_SSL_LIBRARY_UNKNOWN,
	CURLHELP_SSL_LIBRARY_OPENSSL,
	CURLHELP_SSL_LIBRARY_LIBRESSL,
	CURLHELP_SSL_LIBRARY_GNUTLS,
	CURLHELP_SSL_LIBRARY_NSS
} curlhelp_ssl_library;

enum {
	REGS = 2,
};

#include "regex.h"

// Globals
static bool curl_global_initialized = false;
static bool curl_easy_initialized = false;
static int verbose = 0;
static bool body_buf_initialized = false;
static curlhelp_write_curlbuf body_buf;
static bool header_buf_initialized = false;
static curlhelp_write_curlbuf header_buf;
static bool status_line_initialized = false;
static curlhelp_statusline status_line;
static bool put_buf_initialized = false;
static curlhelp_read_curlbuf put_buf;

static struct curl_slist *server_ips = NULL; // TODO maybe unused
static int redir_depth = 0;                  // Maybe global
static CURL *curl;
static struct curl_slist *header_list = NULL;
static long code;
static char errbuf[MAX_INPUT_BUFFER];
static char msg[DEFAULT_BUFFER_SIZE];
typedef union {
	struct curl_slist *to_info;
	struct curl_certinfo *to_certinfo;
} cert_ptr_union;
static cert_ptr_union cert_ptr;
static bool is_openssl_callback = false;
static bool add_sslctx_verify_fun = false;

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
static X509 *cert = NULL;
#endif /* defined(HAVE_SSL) && defined(USE_OPENSSL) */

static int address_family = AF_UNSPEC;
static curlhelp_ssl_library ssl_library = CURLHELP_SSL_LIBRARY_UNKNOWN;

typedef struct {
	int errorcode;
	check_curl_config config;
} check_curl_config_wrapper;
static check_curl_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static void handle_curl_option_return_code(CURLcode res, const char *option);
static mp_state_enum check_http(check_curl_config /*config*/);
static void redir(curlhelp_write_curlbuf * /*header_buf*/, check_curl_config /*config*/);
static char *perfd_time(double elapsed_time, thresholds * /*thlds*/, long /*socket_timeout*/);
static char *perfd_time_connect(double elapsed_time_connect, long /*socket_timeout*/);
static char *perfd_time_ssl(double elapsed_time_ssl, long /*socket_timeout*/);
static char *perfd_time_firstbyte(double elapsed_time_firstbyte, long /*socket_timeout*/);
static char *perfd_time_headers(double elapsed_time_headers, long /*socket_timeout*/);
static char *perfd_time_transfer(double elapsed_time_transfer, long /*socket_timeout*/);
static char *perfd_size(int page_len, int /*min_page_len*/);
static void print_help(void);
void print_usage(void);
static void print_curl_version(void);
static int curlhelp_initwritebuffer(curlhelp_write_curlbuf * /*buf*/);
static size_t curlhelp_buffer_write_callback(void * /*buffer*/, size_t /*size*/, size_t /*nmemb*/, void * /*stream*/);
static void curlhelp_freewritebuffer(curlhelp_write_curlbuf * /*buf*/);
static int curlhelp_initreadbuffer(curlhelp_read_curlbuf * /*buf*/, const char * /*data*/, size_t /*datalen*/);
static size_t curlhelp_buffer_read_callback(void * /*buffer*/, size_t /*size*/, size_t /*nmemb*/, void * /*stream*/);
static void curlhelp_freereadbuffer(curlhelp_read_curlbuf * /*buf*/);
static curlhelp_ssl_library curlhelp_get_ssl_library(void);
static const char *curlhelp_get_ssl_library_string(curlhelp_ssl_library /*ssl_library*/);
int net_noopenssl_check_certificate(cert_ptr_union *, int, int);

static int curlhelp_parse_statusline(const char * /*buf*/, curlhelp_statusline * /*status_line*/);
static void curlhelp_free_statusline(curlhelp_statusline * /*status_line*/);
static char *get_header_value(const struct phr_header *headers, size_t nof_headers, const char *header);
static int check_document_dates(const curlhelp_write_curlbuf * /*header_buf*/, char (*msg)[DEFAULT_BUFFER_SIZE], int /*maximum_age*/);
static int get_content_length(const curlhelp_write_curlbuf *header_buf, const curlhelp_write_curlbuf *body_buf);

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
int np_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn, int days_till_exp_crit);
#endif /* defined(HAVE_SSL) && defined(USE_OPENSSL) */

static void test_file(char * /*path*/);

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	/* parse arguments */
	check_curl_config_wrapper tmp_config = process_arguments(argc, argv);
	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	const check_curl_config config = tmp_config.config;

	/* set defaults */
	if (config.user_agent == NULL) {
		snprintf(config.user_agent, DEFAULT_BUFFER_SIZE, "%s/v%s (monitoring-plugins %s, %s)", progname, NP_VERSION, VERSION,
				 curl_version());
	}

	if (config.display_html) {
		printf("<A HREF=\"%s://%s:%d%s\" target=\"_blank\">", config.use_ssl ? "https" : "http",
			   config.host_name ? config.host_name : config.server_address, config.virtual_port ? config.virtual_port : config.server_port,
			   config.server_url);
	}

	exit(check_http(config));
}

#ifdef HAVE_SSL
#	ifdef USE_OPENSSL

int verify_callback(int preverify_ok, X509_STORE_CTX *x509_ctx) {
	(void)preverify_ok;
	/* TODO: we get all certificates of the chain, so which ones
	 * should we test?
	 * TODO: is the last certificate always the server certificate?
	 */
	cert = X509_STORE_CTX_get_current_cert(x509_ctx);
#		if OPENSSL_VERSION_NUMBER >= 0x10100000L
	X509_up_ref(cert);
#		endif
	if (verbose >= 2) {
		puts("* SSL verify callback with certificate:");
		X509_NAME *subject;
		X509_NAME *issuer;
		printf("*   issuer:\n");
		issuer = X509_get_issuer_name(cert);
		X509_NAME_print_ex_fp(stdout, issuer, 5, XN_FLAG_MULTILINE);
		printf("* curl verify_callback:\n*   subject:\n");
		subject = X509_get_subject_name(cert);
		X509_NAME_print_ex_fp(stdout, subject, 5, XN_FLAG_MULTILINE);
		puts("");
	}
	return 1;
}

CURLcode sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm) {
	(void)curl; // ignore unused parameter
	(void)parm; // ignore unused parameter
	if (add_sslctx_verify_fun) {
		SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, verify_callback);
	}

	// workaround for issue:
	// OpenSSL SSL_read: error:0A000126:SSL routines::unexpected eof while reading, errno 0
	// see discussion https://github.com/openssl/openssl/discussions/22690
#		ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
	SSL_CTX_set_options(sslctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#		endif

	return CURLE_OK;
}

#	endif /* USE_OPENSSL */
#endif     /* HAVE_SSL */

/* returns a string "HTTP/1.x" or "HTTP/2" */
static char *string_statuscode(int major, int minor) {
	static char buf[10];

	switch (major) {
	case 1:
		snprintf(buf, sizeof(buf), "HTTP/%d.%d", major, minor);
		break;
	case 2:
	case 3:
		snprintf(buf, sizeof(buf), "HTTP/%d", major);
		break;
	default:
		/* assuming here HTTP/N with N>=4 */
		snprintf(buf, sizeof(buf), "HTTP/%d", major);
		break;
	}

	return buf;
}

/* Checks if the server 'reply' is one of the expected 'statuscodes' */
static int expected_statuscode(const char *reply, const char *statuscodes) {
	char *expected;

	if ((expected = strdup(statuscodes)) == NULL) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Memory allocation error\n"));
	}

	char *code;
	int result = 0;
	for (code = strtok(expected, ","); code != NULL; code = strtok(NULL, ",")) {
		if (strstr(reply, code) != NULL) {
			result = 1;
			break;
		}
	}

	free(expected);
	return result;
}

void handle_curl_option_return_code(CURLcode res, const char *option) {
	if (res != CURLE_OK) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, _("Error while setting cURL option '%s': cURL returned %d - %s"), option, res,
				 curl_easy_strerror(res));
		die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
	}
}

int lookup_host(const char *host, char *buf, size_t buflen) {
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = address_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags |= AI_CANONNAME;

	struct addrinfo *result;
	int errcode = getaddrinfo(host, NULL, &hints, &result);
	if (errcode != 0) {
		return errcode;
	}

	strcpy(buf, "");
	struct addrinfo *res = result;

	size_t buflen_remaining = buflen - 1;
	size_t addrstr_len;
	char addrstr[100];
	void *ptr = {0};
	while (res) {
		switch (res->ai_family) {
		case AF_INET:
			ptr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
			break;
		case AF_INET6:
			ptr = &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
			break;
		}

		inet_ntop(res->ai_family, ptr, addrstr, 100);
		if (verbose >= 1) {
			printf("* getaddrinfo IPv%d address: %s\n", res->ai_family == PF_INET6 ? 6 : 4, addrstr);
		}

		// Append all IPs to buf as a comma-separated string
		addrstr_len = strlen(addrstr);
		if (buflen_remaining > addrstr_len + 1) {
			if (buf[0] != '\0') {
				strncat(buf, ",", buflen_remaining);
				buflen_remaining -= 1;
			}
			strncat(buf, addrstr, buflen_remaining);
			buflen_remaining -= addrstr_len;
		}

		res = res->ai_next;
	}

	freeaddrinfo(result);

	return 0;
}

static void cleanup(void) {
	if (status_line_initialized) {
		curlhelp_free_statusline(&status_line);
	}
	status_line_initialized = false;
	if (curl_easy_initialized) {
		curl_easy_cleanup(curl);
	}
	curl_easy_initialized = false;
	if (curl_global_initialized) {
		curl_global_cleanup();
	}
	curl_global_initialized = false;
	if (body_buf_initialized) {
		curlhelp_freewritebuffer(&body_buf);
	}
	body_buf_initialized = false;
	if (header_buf_initialized) {
		curlhelp_freewritebuffer(&header_buf);
	}
	header_buf_initialized = false;
	if (put_buf_initialized) {
		curlhelp_freereadbuffer(&put_buf);
	}
	put_buf_initialized = false;
}

mp_state_enum check_http(check_curl_config config) {
	/* initialize curl */
	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		die(STATE_UNKNOWN, "HTTP UNKNOWN - curl_global_init failed\n");
	}
	curl_global_initialized = true;

	if ((curl = curl_easy_init()) == NULL) {
		die(STATE_UNKNOWN, "HTTP UNKNOWN - curl_easy_init failed\n");
	}
	curl_easy_initialized = true;

	/* register cleanup function to shut down libcurl properly */
	atexit(cleanup);

	if (verbose >= 1) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_VERBOSE, 1), "CURLOPT_VERBOSE");
	}

	/* print everything on stdout like check_http would do */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_STDERR, stdout), "CURLOPT_STDERR");

	if (config.automatic_decompression)
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 21, 6)
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""), "CURLOPT_ACCEPT_ENCODING");
#else
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_ENCODING, ""), "CURLOPT_ENCODING");
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 21, 6) */

	/* initialize buffer for body of the answer */
	if (curlhelp_initwritebuffer(&body_buf) < 0) {
		die(STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for body\n");
	}
	body_buf_initialized = true;
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (curl_write_callback)curlhelp_buffer_write_callback),
								   "CURLOPT_WRITEFUNCTION");
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&body_buf), "CURLOPT_WRITEDATA");

	/* initialize buffer for header of the answer */
	if (curlhelp_initwritebuffer(&header_buf) < 0) {
		die(STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for header\n");
	}
	header_buf_initialized = true;
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, (curl_write_callback)curlhelp_buffer_write_callback),
								   "CURLOPT_HEADERFUNCTION");
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_WRITEHEADER, (void *)&header_buf), "CURLOPT_WRITEHEADER");

	/* set the error buffer */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf), "CURLOPT_ERRORBUFFER");

	/* set timeouts */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config.socket_timeout), "CURLOPT_CONNECTTIMEOUT");
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.socket_timeout), "CURLOPT_TIMEOUT");

	/* enable haproxy protocol */
	if (config.haproxy_protocol) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_HAPROXYPROTOCOL, 1L), "CURLOPT_HAPROXYPROTOCOL");
	}

	// fill dns resolve cache to make curl connect to the given server_address instead of the host_name, only required for ssl, because we
	// use the host_name later on to make SNI happy
	struct curl_slist *host = NULL;
	char dnscache[DEFAULT_BUFFER_SIZE];
	char addrstr[DEFAULT_BUFFER_SIZE / 2];
	if (config.use_ssl && config.host_name != NULL) {
		CURLcode res;
		if ((res = lookup_host(config.server_address, addrstr, DEFAULT_BUFFER_SIZE / 2)) != 0) {
			snprintf(msg, DEFAULT_BUFFER_SIZE, _("Unable to lookup IP address for '%s': getaddrinfo returned %d - %s"),
					 config.server_address, res, gai_strerror(res));
			die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
		}
		snprintf(dnscache, DEFAULT_BUFFER_SIZE, "%s:%d:%s", config.host_name, config.server_port, addrstr);
		host = curl_slist_append(NULL, dnscache);
		curl_easy_setopt(curl, CURLOPT_RESOLVE, host);
		if (verbose >= 1) {
			printf("* curl CURLOPT_RESOLVE: %s\n", dnscache);
		}
	}

	// If server_address is an IPv6 address it must be surround by square brackets
	struct in6_addr tmp_in_addr;
	if (inet_pton(AF_INET6, config.server_address, &tmp_in_addr) == 1) {
		char *new_server_address = malloc(strlen(config.server_address) + 3);
		if (new_server_address == NULL) {
			die(STATE_UNKNOWN, "HTTP UNKNOWN - Unable to allocate memory\n");
		}
		snprintf(new_server_address, strlen(config.server_address) + 3, "[%s]", config.server_address);
		free(config.server_address);
		config.server_address = new_server_address;
	}

	/* compose URL: use the address we want to connect to, set Host: header later */
	char url[DEFAULT_BUFFER_SIZE];
	snprintf(url, DEFAULT_BUFFER_SIZE, "%s://%s:%d%s", config.use_ssl ? "https" : "http",
			 (config.use_ssl & (config.host_name != NULL)) ? config.host_name : config.server_address, config.server_port,
			 config.server_url);

	if (verbose >= 1) {
		printf("* curl CURLOPT_URL: %s\n", url);
	}
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_URL, url), "CURLOPT_URL");

	/* extract proxy information for legacy proxy https requests */
	if (!strcmp(config.http_method, "CONNECT") || strstr(config.server_url, "http") == config.server_url) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_PROXY, config.server_address), "CURLOPT_PROXY");
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_PROXYPORT, (long)config.server_port), "CURLOPT_PROXYPORT");
		if (verbose >= 2) {
			printf("* curl CURLOPT_PROXY: %s:%d\n", config.server_address, config.server_port);
		}
		config.http_method = "GET";
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_URL, config.server_url), "CURLOPT_URL");
	}

	/* disable body for HEAD request */
	if (config.http_method && !strcmp(config.http_method, "HEAD")) {
		config.no_body = true;
	}

	/* set HTTP protocol version */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, config.curl_http_version), "CURLOPT_HTTP_VERSION");

	/* set HTTP method */
	if (config.http_method) {
		if (!strcmp(config.http_method, "POST")) {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_POST, 1), "CURLOPT_POST");
		} else if (!strcmp(config.http_method, "PUT")) {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_UPLOAD, 1), "CURLOPT_UPLOAD");
		} else {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, config.http_method), "CURLOPT_CUSTOMREQUEST");
		}
	}

	char *force_host_header = NULL;
	/* check if Host header is explicitly set in options */
	if (config.http_opt_headers_count) {
		for (int i = 0; i < config.http_opt_headers_count; i++) {
			if (strncmp(config.http_opt_headers[i], "Host:", 5) == 0) {
				force_host_header = config.http_opt_headers[i];
			}
		}
	}

	/* set hostname (virtual hosts), not needed if CURLOPT_CONNECT_TO is used, but left in anyway */
	char http_header[DEFAULT_BUFFER_SIZE];
	if (config.host_name != NULL && force_host_header == NULL) {
		if ((config.virtual_port != HTTP_PORT && !config.use_ssl) || (config.virtual_port != HTTPS_PORT && config.use_ssl)) {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s:%d", config.host_name, config.virtual_port);
		} else {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s", config.host_name);
		}
		header_list = curl_slist_append(header_list, http_header);
	}

	/* always close connection, be nice to servers */
	snprintf(http_header, DEFAULT_BUFFER_SIZE, "Connection: close");
	header_list = curl_slist_append(header_list, http_header);

	/* attach additional headers supplied by the user */
	/* optionally send any other header tag */
	if (config.http_opt_headers_count) {
		for (int i = 0; i < config.http_opt_headers_count; i++) {
			header_list = curl_slist_append(header_list, config.http_opt_headers[i]);
		}
		/* This cannot be free'd here because a redirection will then try to access this and segfault */
		/* Covered in a testcase in tests/check_http.t */
		/* free(http_opt_headers); */
	}

	/* set HTTP headers */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list), "CURLOPT_HTTPHEADER");

#ifdef LIBCURL_FEATURE_SSL

	/* set SSL version, warn about insecure or unsupported versions */
	if (config.use_ssl) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSLVERSION, config.ssl_version), "CURLOPT_SSLVERSION");
	}

	/* client certificate and key to present to server (SSL) */
	if (config.client_cert) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSLCERT, config.client_cert), "CURLOPT_SSLCERT");
	}
	if (config.client_privkey) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSLKEY, config.client_privkey), "CURLOPT_SSLKEY");
	}
	if (config.ca_cert) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CAINFO, config.ca_cert), "CURLOPT_CAINFO");
	}
	if (config.ca_cert || config.verify_peer_and_host) {
		/* per default if we have a CA verify both the peer and the
		 * hostname in the certificate, can be switched off later */
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1), "CURLOPT_SSL_VERIFYPEER");
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2), "CURLOPT_SSL_VERIFYHOST");
	} else {
		/* backward-compatible behaviour, be tolerant in checks
		 * TODO: depending on more options have aspects we want
		 * to be less tolerant about ssl verfications
		 */
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0), "CURLOPT_SSL_VERIFYPEER");
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0), "CURLOPT_SSL_VERIFYHOST");
	}

	/* detect SSL library used by libcurl */
	ssl_library = curlhelp_get_ssl_library();

	/* try hard to get a stack of certificates to verify against */
	if (config.check_cert) {
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1)
		/* inform curl to report back certificates */
		switch (ssl_library) {
		case CURLHELP_SSL_LIBRARY_OPENSSL:
		case CURLHELP_SSL_LIBRARY_LIBRESSL:
			/* set callback to extract certificate with OpenSSL context function (works with
			 * OpenSSL-style libraries only!) */
#		ifdef USE_OPENSSL
			/* libcurl and monitoring plugins built with OpenSSL, good */
			add_sslctx_verify_fun = true;
			is_openssl_callback = true;
#		endif /* USE_OPENSSL */
			/* libcurl is built with OpenSSL, monitoring plugins, so falling
			 * back to manually extracting certificate information */
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
			break;

		case CURLHELP_SSL_LIBRARY_NSS:
#		if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
			/* NSS: support for CERTINFO is implemented since 7.34.0 */
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
#		else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			die(STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (libcurl linked with SSL library '%s' is too old)\n",
				curlhelp_get_ssl_library_string(ssl_library));
#		endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			break;

		case CURLHELP_SSL_LIBRARY_GNUTLS:
#		if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0)
			/* GnuTLS: support for CERTINFO is implemented since 7.42.0 */
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
#		else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0) */
			die(STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (libcurl linked with SSL library '%s' is too old)\n",
				curlhelp_get_ssl_library_string(ssl_library));
#		endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0) */
			break;

		case CURLHELP_SSL_LIBRARY_UNKNOWN:
		default:
			die(STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (unknown SSL library '%s', must implement first)\n",
				curlhelp_get_ssl_library_string(ssl_library));
			break;
		}
#	else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */
		/* old libcurl, our only hope is OpenSSL, otherwise we are out of luck */
		if (ssl_library == CURLHELP_SSL_LIBRARY_OPENSSL || ssl_library == CURLHELP_SSL_LIBRARY_LIBRESSL) {
			add_sslctx_verify_fun = true;
		} else {
			die(STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (no CURLOPT_SSL_CTX_FUNCTION, no OpenSSL library or libcurl "
								"too old and has no CURLOPT_CERTINFO)\n");
		}
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */
	}

#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 10, 6) /* required for CURLOPT_SSL_CTX_FUNCTION */
	// ssl ctx function is not available with all ssl backends
	if (curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, NULL) != CURLE_UNKNOWN_OPTION) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun), "CURLOPT_SSL_CTX_FUNCTION");
	}
#	endif

#endif /* LIBCURL_FEATURE_SSL */

	/* set default or user-given user agent identification */
	handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_USERAGENT, config.user_agent), "CURLOPT_USERAGENT");

	/* proxy-authentication */
	if (strcmp(config.proxy_auth, "")) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_PROXYUSERPWD, config.proxy_auth), "CURLOPT_PROXYUSERPWD");
	}

	/* authentication */
	if (strcmp(config.user_auth, "")) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_USERPWD, config.user_auth), "CURLOPT_USERPWD");
	}

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
	if (config.onredirect == STATE_DEPENDENT) {
		if (config.followmethod == FOLLOW_LIBCURL) {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1), "CURLOPT_FOLLOWLOCATION");

			/* default -1 is infinite, not good, could lead to zombie plugins!
			   Setting it to one bigger than maximal limit to handle errors nicely below
			 */
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config.max_depth + 1), "CURLOPT_MAXREDIRS");

			/* for now allow only http and https (we are a http(s) check plugin in the end) */
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 85, 0)
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https"),
										   "CURLOPT_REDIR_PROTOCOLS_STR");
#elif LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 4)
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS),
										   "CURLOPT_REDIRECT_PROTOCOLS");
#endif

			/* TODO: handle the following aspects of redirection, make them
			 * command line options too later:
			  CURLOPT_POSTREDIR: method switch
			  CURLINFO_REDIRECT_URL: custom redirect option
			  CURLOPT_REDIRECT_PROTOCOLS: allow people to step outside safe protocols
			  CURLINFO_REDIRECT_COUNT: get the number of redirects, print it, maybe a range option here is nice like for expected page size?
			*/
		} else {
			/* old style redirection is handled below */
		}
	}

	/* no-body */
	if (config.no_body) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_NOBODY, 1), "CURLOPT_NOBODY");
	}

	/* IPv4 or IPv6 forced DNS resolution */
	if (address_family == AF_UNSPEC) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER),
									   "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_WHATEVER)");
	} else if (address_family == AF_INET) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4),
									   "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V4)");
	}
#if defined(USE_IPV6) && defined(LIBCURL_FEATURE_IPV6)
	else if (address_family == AF_INET6) {
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6),
									   "CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V6)");
	}
#endif

	/* either send http POST data (any data, not only POST)*/
	if (!strcmp(config.http_method, "POST") || !strcmp(config.http_method, "PUT")) {
		/* set content of payload for POST and PUT */
		if (config.http_content_type) {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Content-Type: %s", config.http_content_type);
			header_list = curl_slist_append(header_list, http_header);
		}
		/* NULL indicates "HTTP Continue" in libcurl, provide an empty string
		 * in case of no POST/PUT data */
		if (!config.http_post_data) {
			config.http_post_data = "";
		}
		if (!strcmp(config.http_method, "POST")) {
			/* POST method, set payload with CURLOPT_POSTFIELDS */
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, config.http_post_data), "CURLOPT_POSTFIELDS");
		} else if (!strcmp(config.http_method, "PUT")) {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_READFUNCTION, (curl_read_callback)curlhelp_buffer_read_callback),
										   "CURLOPT_READFUNCTION");
			if (curlhelp_initreadbuffer(&put_buf, config.http_post_data, strlen(config.http_post_data)) < 0) {
				die(STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating read buffer for PUT\n");
			}
			put_buf_initialized = true;
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_READDATA, (void *)&put_buf), "CURLOPT_READDATA");
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_INFILESIZE, (curl_off_t)strlen(config.http_post_data)),
										   "CURLOPT_INFILESIZE");
		}
	}

	/* cookie handling */
	if (config.cookie_jar_file != NULL) {
		/* enable reading cookies from a file, and if the filename is an empty string, only enable the curl cookie engine */
		handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_COOKIEFILE, config.cookie_jar_file), "CURLOPT_COOKIEFILE");
		/* now enable saving cookies to a file, but only if the filename is not an empty string, since writing it would fail */
		if (*config.cookie_jar_file) {
			handle_curl_option_return_code(curl_easy_setopt(curl, CURLOPT_COOKIEJAR, config.cookie_jar_file), "CURLOPT_COOKIEJAR");
		}
	}

	/* do the request */
	CURLcode res = curl_easy_perform(curl);

	if (verbose >= 2 && config.http_post_data) {
		printf("**** REQUEST CONTENT ****\n%s\n", config.http_post_data);
	}

	/* free header and server IP resolve lists, we don't need it anymore */
	curl_slist_free_all(header_list);
	header_list = NULL;
	curl_slist_free_all(server_ips);
	server_ips = NULL;
	if (host) {
		curl_slist_free_all(host);
		host = NULL;
	}

	/* Curl errors, result in critical Nagios state */
	if (res != CURLE_OK) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host on port %d: cURL returned %d - %s"),
				 config.server_port, res, errbuf[0] ? errbuf : curl_easy_strerror(res));
		die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
	}

	int result_ssl = STATE_OK;
	/* certificate checks */
#ifdef LIBCURL_FEATURE_SSL
	if (config.use_ssl) {
		if (config.check_cert) {
			if (is_openssl_callback) {
#	ifdef USE_OPENSSL
				/* check certificate with OpenSSL functions, curl has been built against OpenSSL
				 * and we actually have OpenSSL in the monitoring tools
				 */
				result_ssl = np_net_ssl_check_certificate(cert, config.days_till_exp_warn, config.days_till_exp_crit);
				if (!config.continue_after_check_cert) {
					return result_ssl;
				}
#	else  /* USE_OPENSSL */
				die(STATE_CRITICAL,
					"HTTP CRITICAL - Cannot retrieve certificates - OpenSSL callback used and not linked against OpenSSL\n");
#	endif /* USE_OPENSSL */
			} else {
				struct curl_slist *slist;

				cert_ptr.to_info = NULL;
				res = curl_easy_getinfo(curl, CURLINFO_CERTINFO, &cert_ptr.to_info);
				if (!res && cert_ptr.to_info) {
#	ifdef USE_OPENSSL
					/* We have no OpenSSL in libcurl, but we can use OpenSSL for X509 cert parsing
					 * We only check the first certificate and assume it's the one of the server
					 */
					const char *raw_cert = NULL;
					for (int i = 0; i < cert_ptr.to_certinfo->num_of_certs; i++) {
						for (slist = cert_ptr.to_certinfo->certinfo[i]; slist; slist = slist->next) {
							if (verbose >= 2) {
								printf("%d ** %s\n", i, slist->data);
							}
							if (strncmp(slist->data, "Cert:", 5) == 0) {
								raw_cert = &slist->data[5];
								goto GOT_FIRST_CERT;
							}
						}
					}
				GOT_FIRST_CERT:
					if (!raw_cert) {
						snprintf(msg, DEFAULT_BUFFER_SIZE,
								 _("Cannot retrieve certificates from CERTINFO information - certificate data was empty"));
						die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
					}
					BIO *cert_BIO = BIO_new(BIO_s_mem());
					BIO_write(cert_BIO, raw_cert, strlen(raw_cert));
					cert = PEM_read_bio_X509(cert_BIO, NULL, NULL, NULL);
					if (!cert) {
						snprintf(msg, DEFAULT_BUFFER_SIZE, _("Cannot read certificate from CERTINFO information - BIO error"));
						die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
					}
					BIO_free(cert_BIO);
					result_ssl = np_net_ssl_check_certificate(cert, config.days_till_exp_warn, config.days_till_exp_crit);
					if (!config.continue_after_check_cert) {
						return result_ssl;
					}
#	else  /* USE_OPENSSL */
					/* We assume we don't have OpenSSL and np_net_ssl_check_certificate at our disposal,
					 * so we use the libcurl CURLINFO data
					 */
					result_ssl = net_noopenssl_check_certificate(&cert_ptr, days_till_exp_warn, days_till_exp_crit);
					if (!continue_after_check_cert) {
						return result_ssl;
					}
#	endif /* USE_OPENSSL */
				} else {
					snprintf(msg, DEFAULT_BUFFER_SIZE, _("Cannot retrieve certificates - cURL returned %d - %s"), res,
							 curl_easy_strerror(res));
					die(STATE_CRITICAL, "HTTP CRITICAL - %s\n", msg);
				}
			}
		}
	}
#endif /* LIBCURL_FEATURE_SSL */

	/* we got the data and we executed the request in a given time, so we can append
	 * performance data to the answer always
	 */
	double total_time;
	handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time), "CURLINFO_TOTAL_TIME");
	int page_len = get_content_length(&header_buf, &body_buf);
	char perfstring[DEFAULT_BUFFER_SIZE];
	if (config.show_extended_perfdata) {
		double time_connect;
		handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &time_connect), "CURLINFO_CONNECT_TIME");
		double time_appconnect;
		handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_APPCONNECT_TIME, &time_appconnect), "CURLINFO_APPCONNECT_TIME");
		double time_headers;
		handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME, &time_headers), "CURLINFO_PRETRANSFER_TIME");
		double time_firstbyte;
		handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME, &time_firstbyte),
									   "CURLINFO_STARTTRANSFER_TIME");

		snprintf(perfstring, DEFAULT_BUFFER_SIZE, "%s %s %s %s %s %s %s", perfd_time(total_time, config.thlds, config.socket_timeout),
				 perfd_size(page_len, config.min_page_len), perfd_time_connect(time_connect, config.socket_timeout),
				 config.use_ssl ? perfd_time_ssl(time_appconnect - time_connect, config.socket_timeout) : "",
				 perfd_time_headers(time_headers - time_appconnect, config.socket_timeout),
				 perfd_time_firstbyte(time_firstbyte - time_headers, config.socket_timeout),
				 perfd_time_transfer(total_time - time_firstbyte, config.socket_timeout));
	} else {
		snprintf(perfstring, DEFAULT_BUFFER_SIZE, "%s %s", perfd_time(total_time, config.thlds, config.socket_timeout),
				 perfd_size(page_len, config.min_page_len));
	}

	/* return a CRITICAL status if we couldn't read any data */
	if (strlen(header_buf.buf) == 0 && strlen(body_buf.buf) == 0) {
		die(STATE_CRITICAL, _("HTTP CRITICAL - No header received from host\n"));
	}

	/* get status line of answer, check sanity of HTTP code */
	if (curlhelp_parse_statusline(header_buf.buf, &status_line) < 0) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, "Unparsable status line in %.3g seconds response time|%s\n", total_time, perfstring);
		/* we cannot know the major/minor version here for sure as we cannot parse the first line */
		die(STATE_CRITICAL, "HTTP CRITICAL HTTP/x.x %ld unknown - %s", code, msg);
	}
	status_line_initialized = true;

	/* get result code from cURL */
	handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code), "CURLINFO_RESPONSE_CODE");
	if (verbose >= 2) {
		printf("* curl CURLINFO_RESPONSE_CODE is %ld\n", code);
	}

	/* print status line, header, body if verbose */
	if (verbose >= 2) {
		printf("**** HEADER ****\n%s\n**** CONTENT ****\n%s\n", header_buf.buf, (config.no_body ? "  [[ skipped ]]" : body_buf.buf));
	}

	/* make sure the status line matches the response we are looking for */
	if (!expected_statuscode(status_line.first_line, config.server_expect)) {
		if (config.server_port == HTTP_PORT) {
			snprintf(msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host: %s\n"), status_line.first_line);
		} else {
			snprintf(msg, DEFAULT_BUFFER_SIZE, _("Invalid HTTP response received from host on port %d: %s\n"), config.server_port,
					 status_line.first_line);
		}
		die(STATE_CRITICAL, "HTTP CRITICAL - %s%s%s", msg, config.show_body ? "\n" : "", config.show_body ? body_buf.buf : "");
	}

	int result = STATE_OK;
	if (config.server_expect_yn) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, _("Status line output matched \"%s\" - "), config.server_expect);
		if (verbose) {
			printf("%s\n", msg);
		}
		result = STATE_OK;
	} else {
		/* illegal return codes result in a critical state */
		if (code >= 600 || code < 100) {
			die(STATE_CRITICAL, _("HTTP CRITICAL: Invalid Status (%d, %.40s)\n"), status_line.http_code, status_line.msg);
			/* server errors result in a critical state */
		} else if (code >= 500) {
			result = STATE_CRITICAL;
			/* client errors result in a warning state */
		} else if (code >= 400) {
			result = STATE_WARNING;
			/* check redirected page if specified */
		} else if (code >= 300) {
			if (config.onredirect == STATE_DEPENDENT) {
				if (config.followmethod == FOLLOW_LIBCURL) {
					code = status_line.http_code;
				} else {
					/* old check_http style redirection, if we come
					 * back here, we are in the same status as with
					 * the libcurl method
					 */
					redir(&header_buf, config);
				}
			} else {
				/* this is a specific code in the command line to
				 * be returned when a redirection is encountered
				 */
			}
			result = max_state_alt(config.onredirect, result);
			/* all other codes are considered ok */
		} else {
			result = STATE_OK;
		}
	}

	/* libcurl redirection internally, handle error states here */
	if (config.followmethod == FOLLOW_LIBCURL) {
		handle_curl_option_return_code(curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &redir_depth), "CURLINFO_REDIRECT_COUNT");
		if (verbose >= 2) {
			printf(_("* curl LIBINFO_REDIRECT_COUNT is %d\n"), redir_depth);
		}
		if (redir_depth > config.max_depth) {
			snprintf(msg, DEFAULT_BUFFER_SIZE, "maximum redirection depth %d exceeded in libcurl", config.max_depth);
			die(STATE_WARNING, "HTTP WARNING - %s", msg);
		}
	}

	/* check status codes, set exit status accordingly */
	if (status_line.http_code != code) {
		die(STATE_CRITICAL, _("HTTP CRITICAL %s %d %s - different HTTP codes (cUrl has %ld)\n"),
			string_statuscode(status_line.http_major, status_line.http_minor), status_line.http_code, status_line.msg, code);
	}

	if (config.maximum_age >= 0) {
		result = max_state_alt(check_document_dates(&header_buf, &msg, config.maximum_age), result);
	}

	/* Page and Header content checks go here */

	if (strlen(config.header_expect)) {
		if (!strstr(header_buf.buf, config.header_expect)) {

			char output_header_search[30] = "";
			strncpy(&output_header_search[0], config.header_expect, sizeof(output_header_search));

			if (output_header_search[sizeof(output_header_search) - 1] != '\0') {
				bcopy("...", &output_header_search[sizeof(output_header_search) - 4], 4);
			}

			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sheader '%s' not found on '%s://%s:%d%s', "), msg, output_header_search,
					 config.use_ssl ? "https" : "http", config.host_name ? config.host_name : config.server_address, config.server_port,
					 config.server_url);

			strcpy(msg, tmp);

			result = STATE_CRITICAL;
		}
	}

	if (strlen(config.string_expect)) {
		if (!strstr(body_buf.buf, config.string_expect)) {

			char output_string_search[30] = "";
			strncpy(&output_string_search[0], config.string_expect, sizeof(output_string_search));

			if (output_string_search[sizeof(output_string_search) - 1] != '\0') {
				bcopy("...", &output_string_search[sizeof(output_string_search) - 4], 4);
			}

			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sstring '%s' not found on '%s://%s:%d%s', "), msg, output_string_search,
					 config.use_ssl ? "https" : "http", config.host_name ? config.host_name : config.server_address, config.server_port,
					 config.server_url);

			strcpy(msg, tmp);

			result = STATE_CRITICAL;
		}
	}

	if (strlen(config.regexp)) {
		regex_t preg;
		regmatch_t pmatch[REGS];
		int errcode = regexec(&preg, body_buf.buf, REGS, pmatch, 0);
		if ((errcode == 0 && !config.invert_regex) || (errcode == REG_NOMATCH && config.invert_regex)) {
			/* OK - No-op to avoid changing the logic around it */
			result = max_state_alt(STATE_OK, result);
		} else if ((errcode == REG_NOMATCH && !config.invert_regex) || (errcode == 0 && config.invert_regex)) {
			if (!config.invert_regex) {
				char tmp[DEFAULT_BUFFER_SIZE];

				snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%spattern not found, "), msg);
				strcpy(msg, tmp);

			} else {
				char tmp[DEFAULT_BUFFER_SIZE];

				snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%spattern found, "), msg);
				strcpy(msg, tmp);
			}
			result = config.state_regex;
		} else {
			regerror(errcode, &preg, errbuf, MAX_INPUT_BUFFER);

			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sExecute Error: %s, "), msg, errbuf);
			strcpy(msg, tmp);
			result = STATE_UNKNOWN;
		}
	}

	/* make sure the page is of an appropriate size */
	if ((config.max_page_len > 0) && (page_len > config.max_page_len)) {
		char tmp[DEFAULT_BUFFER_SIZE];

		snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%spage size %d too large, "), msg, page_len);

		strcpy(msg, tmp);

		result = max_state_alt(STATE_WARNING, result);

	} else if ((config.min_page_len > 0) && (page_len < config.min_page_len)) {
		char tmp[DEFAULT_BUFFER_SIZE];

		snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%spage size %d too small, "), msg, page_len);
		strcpy(msg, tmp);
		result = max_state_alt(STATE_WARNING, result);
	}

	/* -w, -c: check warning and critical level */
	result = max_state_alt(get_status(total_time, config.thlds), result);

	/* Cut-off trailing characters */
	if (strlen(msg) >= 2) {
		if (msg[strlen(msg) - 2] == ',') {
			msg[strlen(msg) - 2] = '\0';
		} else {
			msg[strlen(msg) - 3] = '\0';
		}
	}

	/* TODO: separate _() msg and status code: die (result, "HTTP %s: %s\n", state_text(result), msg); */
	die(max_state_alt(result, result_ssl), "HTTP %s: %s %d %s%s%s - %d bytes in %.3f second response time %s|%s\n%s%s", state_text(result),
		string_statuscode(status_line.http_major, status_line.http_minor), status_line.http_code, status_line.msg,
		strlen(msg) > 0 ? " - " : "", msg, page_len, total_time, (config.display_html ? "</A>" : ""), perfstring,
		(config.show_body ? body_buf.buf : ""), (config.show_body ? "\n" : ""));

	return max_state_alt(result, result_ssl);
}

int uri_strcmp(const UriTextRangeA range, const char *s) {
	if (!range.first) {
		return -1;
	}
	if ((size_t)(range.afterLast - range.first) < strlen(s)) {
		return -1;
	}
	return strncmp(s, range.first, min((size_t)(range.afterLast - range.first), strlen(s)));
}

char *uri_string(const UriTextRangeA range, char *buf, size_t buflen) {
	if (!range.first) {
		return "(null)";
	}
	strncpy(buf, range.first, max(buflen - 1, (size_t)(range.afterLast - range.first)));
	buf[max(buflen - 1, (size_t)(range.afterLast - range.first))] = '\0';
	buf[range.afterLast - range.first] = '\0';
	return buf;
}

void redir(curlhelp_write_curlbuf *header_buf, check_curl_config config) {
	curlhelp_statusline status_line;
	struct phr_header headers[255];
	size_t msglen;
	size_t nof_headers = 255;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major, &status_line.http_minor,
								 &status_line.http_code, &status_line.msg, &msglen, headers, &nof_headers, 0);

	if (res == -1) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Failed to parse Response\n"));
	}

	char *location = get_header_value(headers, nof_headers, "location");

	if (verbose >= 2) {
		printf(_("* Seen redirect location %s\n"), location);
	}

	if (++redir_depth > config.max_depth) {
		die(STATE_WARNING, _("HTTP WARNING - maximum redirection depth %d exceeded - %s%s\n"), config.max_depth, location,
			(config.display_html ? "</A>" : ""));
	}

	UriParserStateA state;
	UriUriA uri;
	state.uri = &uri;
	if (uriParseUriA(&state, location) != URI_SUCCESS) {
		if (state.errorCode == URI_ERROR_SYNTAX) {
			die(STATE_UNKNOWN, _("HTTP UNKNOWN - Could not parse redirect location '%s'%s\n"), location,
				(config.display_html ? "</A>" : ""));
		} else if (state.errorCode == URI_ERROR_MALLOC) {
			die(STATE_UNKNOWN, _("HTTP UNKNOWN - Could not allocate URL\n"));
		}
	}

	char ipstr[INET_ADDR_MAX_SIZE];
	char buf[DEFAULT_BUFFER_SIZE];
	if (verbose >= 2) {
		printf(_("** scheme: %s\n"), uri_string(uri.scheme, buf, DEFAULT_BUFFER_SIZE));
		printf(_("** host: %s\n"), uri_string(uri.hostText, buf, DEFAULT_BUFFER_SIZE));
		printf(_("** port: %s\n"), uri_string(uri.portText, buf, DEFAULT_BUFFER_SIZE));
		if (uri.hostData.ip4) {
			inet_ntop(AF_INET, uri.hostData.ip4->data, ipstr, sizeof(ipstr));
			printf(_("** IPv4: %s\n"), ipstr);
		}
		if (uri.hostData.ip6) {
			inet_ntop(AF_INET, uri.hostData.ip6->data, ipstr, sizeof(ipstr));
			printf(_("** IPv6: %s\n"), ipstr);
		}
		if (uri.pathHead) {
			printf(_("** path: "));
			const UriPathSegmentA *p = uri.pathHead;
			for (; p; p = p->next) {
				printf("/%s", uri_string(p->text, buf, DEFAULT_BUFFER_SIZE));
			}
			puts("");
		}
		if (uri.query.first) {
			printf(_("** query: %s\n"), uri_string(uri.query, buf, DEFAULT_BUFFER_SIZE));
		}
		if (uri.fragment.first) {
			printf(_("** fragment: %s\n"), uri_string(uri.fragment, buf, DEFAULT_BUFFER_SIZE));
		}
	}

	if (uri.scheme.first) {
		config.use_ssl = (bool)(!uri_strcmp(uri.scheme, "https"));
	}

	/* we do a sloppy test here only, because uriparser would have failed
	 * above, if the port would be invalid, we just check for MAX_PORT
	 */
	int new_port;
	if (uri.portText.first) {
		new_port = atoi(uri_string(uri.portText, buf, DEFAULT_BUFFER_SIZE));
	} else {
		new_port = HTTP_PORT;
		if (config.use_ssl) {
			new_port = HTTPS_PORT;
		}
	}
	if (new_port > MAX_PORT) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Redirection to port above %d - %s%s\n"), MAX_PORT, location,
			config.display_html ? "</A>" : "");
	}

	/* by RFC 7231 relative URLs in Location should be taken relative to
	 * the original URL, so we try to form a new absolute URL here
	 */
	char *new_host;
	if (!uri.scheme.first && !uri.hostText.first) {
		new_host = strdup(config.host_name ? config.host_name : config.server_address);
		new_port = config.server_port;
		if (config.use_ssl) {
			uri_string(uri.scheme, "https", DEFAULT_BUFFER_SIZE);
		}
	} else {
		new_host = strdup(uri_string(uri.hostText, buf, DEFAULT_BUFFER_SIZE));
	}

	/* compose new path */
	/* TODO: handle fragments and query part of URL */
	char *new_url = (char *)calloc(1, DEFAULT_BUFFER_SIZE);
	if (uri.pathHead) {
		const UriPathSegmentA *p = uri.pathHead;
		for (; p; p = p->next) {
			strncat(new_url, "/", DEFAULT_BUFFER_SIZE);
			strncat(new_url, uri_string(p->text, buf, DEFAULT_BUFFER_SIZE), DEFAULT_BUFFER_SIZE - 1);
		}
	}

	if (config.server_port == new_port && !strncmp(config.server_address, new_host, MAX_IPV4_HOSTLENGTH) &&
		(config.host_name && !strncmp(config.host_name, new_host, MAX_IPV4_HOSTLENGTH)) && !strcmp(config.server_url, new_url)) {
		die(STATE_CRITICAL, _("HTTP CRITICAL - redirection creates an infinite loop - %s://%s:%d%s%s\n"), config.use_ssl ? "https" : "http",
			new_host, new_port, new_url, (config.display_html ? "</A>" : ""));
	}

	/* set new values for redirected request */

	if (!(config.followsticky & STICKY_HOST)) {
		free(config.server_address);
		config.server_address = strndup(new_host, MAX_IPV4_HOSTLENGTH);
	}
	if (!(config.followsticky & STICKY_PORT)) {
		config.server_port = (unsigned short)new_port;
	}

	free(config.host_name);
	config.host_name = strndup(new_host, MAX_IPV4_HOSTLENGTH);

	/* reset virtual port */
	config.virtual_port = config.server_port;

	free(new_host);
	free(config.server_url);
	config.server_url = new_url;

	uriFreeUriMembersA(&uri);

	if (verbose) {
		printf(_("Redirection to %s://%s:%d%s\n"), config.use_ssl ? "https" : "http",
			   config.host_name ? config.host_name : config.server_address, config.server_port, config.server_url);
	}

	/* TODO: the hash component MUST be taken from the original URL and
	 * attached to the URL in Location
	 */

	cleanup();
	check_http(config);
}

/* check whether a file exists */
void test_file(char *path) {
	if (access(path, R_OK) == 0) {
		return;
	}
	usage2(_("file does not exist or is not readable"), path);
}

check_curl_config_wrapper process_arguments(int argc, char **argv) {
	enum {
		INVERT_REGEX = CHAR_MAX + 1,
		SNI_OPTION,
		MAX_REDIRS_OPTION,
		CONTINUE_AFTER_CHECK_CERT,
		CA_CERT_OPTION,
		HTTP_VERSION_OPTION,
		AUTOMATIC_DECOMPRESSION,
		COOKIE_JAR,
		HAPROXY_PROTOCOL,
		STATE_REGEX
	};

	static struct option longopts[] = {STD_LONG_OPTS,
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
									   {"proxy-authorization", required_argument, 0, 'b'},
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
									   {"verify-cert", no_argument, 0, 'D'},
									   {"continue-after-certificate", no_argument, 0, CONTINUE_AFTER_CHECK_CERT},
									   {"useragent", required_argument, 0, 'A'},
									   {"header", required_argument, 0, 'k'},
									   {"no-body", no_argument, 0, 'N'},
									   {"max-age", required_argument, 0, 'M'},
									   {"content-type", required_argument, 0, 'T'},
									   {"pagesize", required_argument, 0, 'm'},
									   {"invert-regex", no_argument, NULL, INVERT_REGEX},
									   {"state-regex", required_argument, 0, STATE_REGEX},
									   {"use-ipv4", no_argument, 0, '4'},
									   {"use-ipv6", no_argument, 0, '6'},
									   {"extended-perfdata", no_argument, 0, 'E'},
									   {"show-body", no_argument, 0, 'B'},
									   {"max-redirs", required_argument, 0, MAX_REDIRS_OPTION},
									   {"http-version", required_argument, 0, HTTP_VERSION_OPTION},
									   {"enable-automatic-decompression", no_argument, 0, AUTOMATIC_DECOMPRESSION},
									   {"cookie-jar", required_argument, 0, COOKIE_JAR},
									   {"haproxy-protocol", no_argument, 0, HAPROXY_PROTOCOL},
									   {0, 0, 0, 0}};

	check_curl_config_wrapper result = {
		.errorcode = OK,
		.config = check_curl_config_init(),
	};

	if (argc < 2) {
		result.errorcode = ERROR;
		return result;
	}

	/* support check_http compatible arguments */
	for (int index = 1; index < argc; index++) {
		if (strcmp("-to", argv[index]) == 0) {
			strcpy(argv[index], "-t");
		}
		if (strcmp("-hn", argv[index]) == 0) {
			strcpy(argv[index], "-H");
		}
		if (strcmp("-wt", argv[index]) == 0) {
			strcpy(argv[index], "-w");
		}
		if (strcmp("-ct", argv[index]) == 0) {
			strcpy(argv[index], "-c");
		}
		if (strcmp("-nohtml", argv[index]) == 0) {
			strcpy(argv[index], "-n");
		}
	}

	int option = 0;
	char *warning_thresholds = NULL;
	char *critical_thresholds = NULL;
	int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
	bool specify_port = false;

	while (true) {
		int option_index = getopt_long(argc, argv, "Vvh46t:c:w:A:k:H:P:j:T:I:a:b:d:e:p:s:R:r:u:f:C:J:K:DnlLS::m:M:NEB", longopts, &option);
		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
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
			if (!is_intnonneg(optarg)) {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			} else {
				result.config.socket_timeout = (int)strtol(optarg, NULL, 10);
			}
			break;
		case 'c': /* critical time threshold */
			critical_thresholds = optarg;
			break;
		case 'w': /* warning time threshold */
			warning_thresholds = optarg;
			break;
		case 'H': /* virtual host */
			result.config.host_name = strdup(optarg);
			char *p;
			int host_name_length;
			if (result.config.host_name[0] == '[') {
				if ((p = strstr(result.config.host_name, "]:")) != NULL) { /* [IPv6]:port */
					result.config.virtual_port = atoi(p + 2);
					/* cut off the port */
					host_name_length = strlen(result.config.host_name) - strlen(p) - 1;
					free(result.config.host_name);
					result.config.host_name = strndup(optarg, host_name_length);
				}
			} else if ((p = strchr(result.config.host_name, ':')) != NULL && strchr(++p, ':') == NULL) { /* IPv4:port or host:port */
				result.config.virtual_port = atoi(p);
				/* cut off the port */
				host_name_length = strlen(result.config.host_name) - strlen(p) - 1;
				free(result.config.host_name);
				result.config.host_name = strndup(optarg, host_name_length);
			}
			break;
		case 'I': /* internet address */
			result.config.server_address = strdup(optarg);
			break;
		case 'u': /* URL path */
			result.config.server_url = strdup(optarg);
			break;
		case 'p': /* Server port */
			if (!is_intnonneg(optarg)) {
				usage2(_("Invalid port number, expecting a non-negative number"), optarg);
			} else {
				if (strtol(optarg, NULL, 10) > MAX_PORT) {
					usage2(_("Invalid port number, supplied port number is too big"), optarg);
				}
				result.config.server_port = (unsigned short)strtol(optarg, NULL, 10);
				specify_port = true;
			}
			break;
		case 'a': /* authorization info */
			strncpy(result.config.user_auth, optarg, MAX_INPUT_BUFFER - 1);
			result.config.user_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'b': /* proxy-authorization info */
			strncpy(result.config.proxy_auth, optarg, MAX_INPUT_BUFFER - 1);
			result.config.proxy_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'P': /* HTTP POST data in URL encoded format; ignored if settings already */
			if (!result.config.http_post_data) {
				result.config.http_post_data = strdup(optarg);
			}
			if (!result.config.http_method) {
				result.config.http_method = strdup("POST");
			}
			break;
		case 'j': /* Set HTTP method */
			if (result.config.http_method) {
				free(result.config.http_method);
			}
			result.config.http_method = strdup(optarg);
			break;
		case 'A': /* useragent */
			strncpy(result.config.user_agent, optarg, DEFAULT_BUFFER_SIZE);
			result.config.user_agent[DEFAULT_BUFFER_SIZE - 1] = '\0';
			break;
		case 'k': /* Additional headers */
			if (result.config.http_opt_headers_count == 0) {
				result.config.http_opt_headers = malloc(sizeof(char *) * (++result.config.http_opt_headers_count));
			} else {
				result.config.http_opt_headers =
					realloc(result.config.http_opt_headers, sizeof(char *) * (++result.config.http_opt_headers_count));
			}
			result.config.http_opt_headers[result.config.http_opt_headers_count - 1] = optarg;
			break;
		case 'L': /* show html link */
			result.config.display_html = true;
			break;
		case 'n': /* do not show html link */
			result.config.display_html = false;
			break;
		case 'C': /* Check SSL cert validity */
#ifdef LIBCURL_FEATURE_SSL
			char *temp;
			if ((temp = strchr(optarg, ',')) != NULL) {
				*temp = '\0';
				if (!is_intnonneg(optarg)) {
					usage2(_("Invalid certificate expiration period"), optarg);
				}
				result.config.days_till_exp_warn = atoi(optarg);
				*temp = ',';
				temp++;
				if (!is_intnonneg(temp)) {
					usage2(_("Invalid certificate expiration period"), temp);
				}
				result.config.days_till_exp_crit = atoi(temp);
			} else {
				result.config.days_till_exp_crit = 0;
				if (!is_intnonneg(optarg)) {
					usage2(_("Invalid certificate expiration period"), optarg);
				}
				result.config.days_till_exp_warn = atoi(optarg);
			}
			result.config.check_cert = true;
			goto enable_ssl;
#endif
		case CONTINUE_AFTER_CHECK_CERT: /* don't stop after the certificate is checked */
#ifdef HAVE_SSL
			result.config.continue_after_check_cert = true;
			break;
#endif
		case 'J': /* use client certificate */
#ifdef LIBCURL_FEATURE_SSL
			test_file(optarg);
			result.config.client_cert = optarg;
			goto enable_ssl;
#endif
		case 'K': /* use client private key */
#ifdef LIBCURL_FEATURE_SSL
			test_file(optarg);
			result.config.client_privkey = optarg;
			goto enable_ssl;
#endif
#ifdef LIBCURL_FEATURE_SSL
		case CA_CERT_OPTION: /* use CA chain file */
			test_file(optarg);
			result.config.ca_cert = optarg;
			goto enable_ssl;
#endif
#ifdef LIBCURL_FEATURE_SSL
		case 'D': /* verify peer certificate & host */
			result.config.verify_peer_and_host = true;
			break;
#endif
		case 'S': /* use SSL */
#ifdef LIBCURL_FEATURE_SSL
		{
		enable_ssl:
			bool got_plus = false;
			result.config.use_ssl = true;
			/* ssl_version initialized to CURL_SSLVERSION_DEFAULT as a default.
			 * Only set if it's non-zero.  This helps when we include multiple
			 * parameters, like -S and -C combinations */
			result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
			if (option_index == 'S' && optarg != NULL) {
				char *plus_ptr = strchr(optarg, '+');
				if (plus_ptr) {
					got_plus = true;
					*plus_ptr = '\0';
				}

				if (optarg[0] == '2') {
					result.config.ssl_version = CURL_SSLVERSION_SSLv2;
				} else if (optarg[0] == '3') {
					result.config.ssl_version = CURL_SSLVERSION_SSLv3;
				} else if (!strcmp(optarg, "1") || !strcmp(optarg, "1.0"))
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
					result.config.ssl_version = CURL_SSLVERSION_TLSv1_0;
#	else
					result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
				else if (!strcmp(optarg, "1.1"))
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
					result.config.ssl_version = CURL_SSLVERSION_TLSv1_1;
#	else
					result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
				else if (!strcmp(optarg, "1.2"))
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
					result.config.ssl_version = CURL_SSLVERSION_TLSv1_2;
#	else
					result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
				else if (!strcmp(optarg, "1.3"))
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0)
					result.config.ssl_version = CURL_SSLVERSION_TLSv1_3;
#	else
					result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0) */
				else {
					usage4(_("Invalid option - Valid SSL/TLS versions: 2, 3, 1, 1.1, 1.2, 1.3 (with optional '+' suffix)"));
				}
			}
#	if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0)
			if (got_plus) {
				switch (result.config.ssl_version) {
				case CURL_SSLVERSION_TLSv1_3:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
					break;
				case CURL_SSLVERSION_TLSv1_2:
				case CURL_SSLVERSION_TLSv1_1:
				case CURL_SSLVERSION_TLSv1_0:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_DEFAULT;
					break;
				}
			} else {
				switch (result.config.ssl_version) {
				case CURL_SSLVERSION_TLSv1_3:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
					break;
				case CURL_SSLVERSION_TLSv1_2:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_2;
					break;
				case CURL_SSLVERSION_TLSv1_1:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_1;
					break;
				case CURL_SSLVERSION_TLSv1_0:
					result.config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_0;
					break;
				}
			}
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0) */
			if (verbose >= 2) {
				printf(_("* Set SSL/TLS version to %d\n"), result.config.ssl_version);
			}
			if (!specify_port) {
				result.config.server_port = HTTPS_PORT;
			}
		} break;
#else  /* LIBCURL_FEATURE_SSL */
			/* -C -J and -K fall through to here without SSL */
			usage4(_("Invalid option - SSL is not available"));
			break;
		case SNI_OPTION: /* --sni is parsed, but ignored, the default is true with libcurl */
			use_sni = true;
			break;
#endif /* LIBCURL_FEATURE_SSL */
		case MAX_REDIRS_OPTION:
			if (!is_intnonneg(optarg)) {
				usage2(_("Invalid max_redirs count"), optarg);
			} else {
				result.config.max_depth = atoi(optarg);
			}
			break;
		case 'f': /* onredirect */
			if (!strcmp(optarg, "ok")) {
				result.config.onredirect = STATE_OK;
			} else if (!strcmp(optarg, "warning")) {
				result.config.onredirect = STATE_WARNING;
			} else if (!strcmp(optarg, "critical")) {
				result.config.onredirect = STATE_CRITICAL;
			} else if (!strcmp(optarg, "unknown")) {
				result.config.onredirect = STATE_UNKNOWN;
			} else if (!strcmp(optarg, "follow")) {
				result.config.onredirect = STATE_DEPENDENT;
			} else if (!strcmp(optarg, "stickyport")) {
				result.config.onredirect = STATE_DEPENDENT, result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_HOST | STICKY_PORT;
			} else if (!strcmp(optarg, "sticky")) {
				result.config.onredirect = STATE_DEPENDENT, result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_HOST;
			} else if (!strcmp(optarg, "follow")) {
				result.config.onredirect = STATE_DEPENDENT, result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_NONE;
			} else if (!strcmp(optarg, "curl")) {
				result.config.onredirect = STATE_DEPENDENT, result.config.followmethod = FOLLOW_LIBCURL;
			} else {
				usage2(_("Invalid onredirect option"), optarg);
			}
			if (verbose >= 2) {
				printf(_("* Following redirects set to %s\n"), state_text(result.config.onredirect));
			}
			break;
		case 'd': /* string or substring */
			strncpy(result.config.header_expect, optarg, MAX_INPUT_BUFFER - 1);
			result.config.header_expect[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 's': /* string or substring */
			strncpy(result.config.string_expect, optarg, MAX_INPUT_BUFFER - 1);
			result.config.string_expect[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'e': /* string or substring */
			strncpy(result.config.server_expect, optarg, MAX_INPUT_BUFFER - 1);
			result.config.server_expect[MAX_INPUT_BUFFER - 1] = 0;
			result.config.server_expect_yn = true;
			break;
		case 'T': /* Content-type */
			result.config.http_content_type = strdup(optarg);
			break;
		case 'l': /* linespan */
			cflags &= ~REG_NEWLINE;
			break;
		case 'R': /* regex */
			cflags |= REG_ICASE;
			// fall through
		case 'r': /* regex */
			strncpy(result.config.regexp, optarg, MAX_RE_SIZE - 1);
			result.config.regexp[MAX_RE_SIZE - 1] = 0;
			regex_t preg;
			int errcode = regcomp(&preg, result.config.regexp, cflags);
			if (errcode != 0) {
				(void)regerror(errcode, &preg, errbuf, MAX_INPUT_BUFFER);
				printf(_("Could Not Compile Regular Expression: %s"), errbuf);
				result.errorcode = ERROR;
				return result;
			}
			break;
		case INVERT_REGEX:
			result.config.invert_regex = true;
			break;
		case STATE_REGEX:
			if (!strcasecmp(optarg, "critical")) {
				result.config.state_regex = STATE_CRITICAL;
			} else if (!strcasecmp(optarg, "warning")) {
				result.config.state_regex = STATE_WARNING;
			} else {
				usage2(_("Invalid state-regex option"), optarg);
			}
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#if defined(USE_IPV6) && defined(LIBCURL_FEATURE_IPV6)
			address_family = AF_INET6;
#else
			usage4(_("IPv6 support not available"));
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
					exit(STATE_WARNING);
				} else {
					result.config.min_page_len = atoi(tmp);
				}

				tmp = strtok(NULL, ":");
				if (tmp == NULL) {
					printf("Bad format: try \"-m min:max\"\n");
					exit(STATE_WARNING);
				} else {
					result.config.max_page_len = atoi(tmp);
				}
			} else {
				result.config.min_page_len = atoi(optarg);
			}
			break;
		}
		case 'N': /* no-body */
			result.config.no_body = true;
			break;
		case 'M': /* max-age */
		{
			size_t option_length = strlen(optarg);
			if (option_length && optarg[option_length - 1] == 'm') {
				result.config.maximum_age = atoi(optarg) * 60;
			} else if (option_length && optarg[option_length - 1] == 'h') {
				result.config.maximum_age = atoi(optarg) * 60 * 60;
			} else if (option_length && optarg[option_length - 1] == 'd') {
				result.config.maximum_age = atoi(optarg) * 60 * 60 * 24;
			} else if (option_length && (optarg[option_length - 1] == 's' || isdigit(optarg[option_length - 1]))) {
				result.config.maximum_age = atoi(optarg);
			} else {
				fprintf(stderr, "unparsable max-age: %s\n", optarg);
				exit(STATE_WARNING);
			}
			if (verbose >= 2) {
				printf("* Maximal age of document set to %d seconds\n", result.config.maximum_age);
			}
		} break;
		case 'E': /* show extended perfdata */
			result.config.show_extended_perfdata = true;
			break;
		case 'B': /* print body content after status line */
			result.config.show_body = true;
			break;
		case HTTP_VERSION_OPTION:
			result.config.curl_http_version = CURL_HTTP_VERSION_NONE;
			if (strcmp(optarg, "1.0") == 0) {
				result.config.curl_http_version = CURL_HTTP_VERSION_1_0;
			} else if (strcmp(optarg, "1.1") == 0) {
				result.config.curl_http_version = CURL_HTTP_VERSION_1_1;
			} else if ((strcmp(optarg, "2.0") == 0) || (strcmp(optarg, "2") == 0)) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 33, 0)
				result.config.curl_http_version = CURL_HTTP_VERSION_2_0;
#else
				result.config.curl_http_version = CURL_HTTP_VERSION_NONE;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 33, 0) */
			} else {
				fprintf(stderr, "unknown http-version parameter: %s\n", optarg);
				exit(STATE_WARNING);
			}
			break;
		case AUTOMATIC_DECOMPRESSION:
			result.config.automatic_decompression = true;
			break;
		case COOKIE_JAR:
			result.config.cookie_jar_file = optarg;
			break;
		case HAPROXY_PROTOCOL:
			result.config.haproxy_protocol = true;
			break;
		case '?':
			/* print short usage statement if args not parsable */
			usage5();
			break;
		}
	}

	int c = optind;

	if (result.config.server_address == NULL && c < argc) {
		result.config.server_address = strdup(argv[c++]);
	}

	if (result.config.host_name == NULL && c < argc) {
		result.config.host_name = strdup(argv[c++]);
	}

	if (result.config.server_address == NULL) {
		if (result.config.host_name == NULL) {
			usage4(_("You must specify a server address or host name"));
		} else {
			result.config.server_address = strdup(result.config.host_name);
		}
	}

	set_thresholds(&result.config.thlds, warning_thresholds, critical_thresholds);

	if (critical_thresholds && result.config.thlds->critical->end > (double)result.config.socket_timeout) {
		result.config.socket_timeout = (int)result.config.thlds->critical->end + 1;
	}
	if (verbose >= 2) {
		printf("* Socket timeout set to %ld seconds\n", result.config.socket_timeout);
	}

	if (result.config.http_method == NULL) {
		result.config.http_method = strdup("GET");
	}

	if (result.config.client_cert && !result.config.client_privkey) {
		usage4(_("If you use a client certificate you must also specify a private key file"));
	}

	if (result.config.virtual_port == 0) {
		result.config.virtual_port = result.config.server_port;
	} else {
		if ((result.config.use_ssl && result.config.server_port == HTTPS_PORT) ||
			(!result.config.use_ssl && result.config.server_port == HTTP_PORT)) {
			if (!specify_port) {
				result.config.server_port = result.config.virtual_port;
			}
		}
	}

	return result;
}

char *perfd_time(double elapsed_time, thresholds *thlds, long socket_timeout) {
	return fperfdata("time", elapsed_time, "s", thlds->warning, thlds->warning ? thlds->warning->end : 0, thlds->critical,
					 thlds->critical ? thlds->critical->end : 0, true, 0, true, socket_timeout);
}

char *perfd_time_connect(double elapsed_time_connect, long socket_timeout) {
	return fperfdata("time_connect", elapsed_time_connect, "s", false, 0, false, 0, false, 0, true, socket_timeout);
}

char *perfd_time_ssl(double elapsed_time_ssl, long socket_timeout) {
	return fperfdata("time_ssl", elapsed_time_ssl, "s", false, 0, false, 0, false, 0, true, socket_timeout);
}

char *perfd_time_headers(double elapsed_time_headers, long socket_timeout) {
	return fperfdata("time_headers", elapsed_time_headers, "s", false, 0, false, 0, false, 0, true, socket_timeout);
}

char *perfd_time_firstbyte(double elapsed_time_firstbyte, long socket_timeout) {
	return fperfdata("time_firstbyte", elapsed_time_firstbyte, "s", false, 0, false, 0, false, 0, true, socket_timeout);
}

char *perfd_time_transfer(double elapsed_time_transfer, long socket_timeout) {
	return fperfdata("time_transfer", elapsed_time_transfer, "s", false, 0, false, 0, false, 0, true, socket_timeout);
}

char *perfd_size(int page_len, int min_page_len) {
	return perfdata("size", page_len, "B", (min_page_len > 0), min_page_len, (min_page_len > 0), 0, true, 0, false, 0);
}

void print_help(void) {
	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Ethan Galstad <nagios@nagios.org>\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin tests the HTTP service on the specified host. It can test"));
	printf("%s\n", _("normal (http) and secure (https) servers, follow redirects, search for"));
	printf("%s\n", _("strings and regular expressions, check connection times, and report on"));
	printf("%s\n", _("certificate expiration times."));
	printf("\n");
	printf("%s\n", _("It makes use of libcurl to do so. It tries to be as compatible to check_http"));
	printf("%s\n", _("as possible."));

	printf("\n\n");

	print_usage();

	printf(_("NOTE: One or both of -H and -I must be specified"));

	printf("\n");

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-H, --hostname=ADDRESS");
	printf("    %s\n", _("Host name argument for servers using host headers (virtual host)"));
	printf("    %s\n", _("Append a port to include it in the header (eg: example.com:5000)"));
	printf(" %s\n", "-I, --IP-address=ADDRESS");
	printf("    %s\n", _("IP address or name (use numeric address if possible to bypass DNS lookup)."));
	printf(" %s\n", "-p, --port=INTEGER");
	printf("    %s", _("Port number (default: "));
	printf("%d)\n", HTTP_PORT);

	printf(UT_IPv46);

#ifdef LIBCURL_FEATURE_SSL
	printf(" %s\n", "-S, --ssl=VERSION[+]");
	printf("    %s\n", _("Connect via SSL. Port defaults to 443. VERSION is optional, and prevents"));
	printf("    %s\n", _("auto-negotiation (2 = SSLv2, 3 = SSLv3, 1 = TLSv1, 1.1 = TLSv1.1,"));
	printf("    %s\n", _("1.2 = TLSv1.2, 1.3 = TLSv1.3). With a '+' suffix, newer versions are also accepted."));
	printf("    %s\n", _("Note: SSLv2, SSLv3, TLSv1.0 and TLSv1.1 are deprecated and are usually disabled in libcurl"));
	printf(" %s\n", "--sni");
	printf("    %s\n", _("Enable SSL/TLS hostname extension support (SNI)"));
#	if LIBCURL_VERSION_NUM >= 0x071801
	printf("    %s\n", _("Note: --sni is the default in libcurl as SSLv2 and SSLV3 are deprecated and"));
	printf("    %s\n", _("      SNI only really works since TLSv1.0"));
#	else
	printf("    %s\n", _("Note: SNI is not supported in libcurl before 7.18.1"));
#	endif
	printf(" %s\n", "-C, --certificate=INTEGER[,INTEGER]");
	printf("    %s\n", _("Minimum number of days a certificate has to be valid. Port defaults to 443."));
	printf("    %s\n", _("A STATE_WARNING is returned if the certificate has a validity less than the"));
	printf("    %s\n", _("first agument's value. If there is a second argument and the certificate's"));
	printf("    %s\n", _("validity is less than its value, a STATE_CRITICAL is returned."));
	printf("    %s\n", _("(When this option is used the URL is not checked by default. You can use"));
	printf("    %s\n", _(" --continue-after-certificate to override this behavior)"));
	printf(" %s\n", "--continue-after-certificate");
	printf("    %s\n", _("Allows the HTTP check to continue after performing the certificate check."));
	printf("    %s\n", _("Does nothing unless -C is used."));
	printf(" %s\n", "-J, --client-cert=FILE");
	printf("   %s\n", _("Name of file that contains the client certificate (PEM format)"));
	printf("   %s\n", _("to be used in establishing the SSL session"));
	printf(" %s\n", "-K, --private-key=FILE");
	printf("   %s\n", _("Name of file containing the private key (PEM format)"));
	printf("   %s\n", _("matching the client certificate"));
	printf(" %s\n", "--ca-cert=FILE");
	printf("   %s\n", _("CA certificate file to verify peer against"));
	printf(" %s\n", "-D, --verify-cert");
	printf("   %s\n", _("Verify the peer's SSL certificate and hostname"));
#endif

	printf(" %s\n", "-e, --expect=STRING");
	printf("    %s\n", _("Comma-delimited list of strings, at least one of them is expected in"));
	printf("    %s", _("the first (status) line of the server response (default: "));
	printf("%s)\n", HTTP_EXPECT);
	printf("    %s\n", _("If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)"));
	printf(" %s\n", "-d, --header-string=STRING");
	printf("    %s\n", _("String to expect in the response headers"));
	printf(" %s\n", "-s, --string=STRING");
	printf("    %s\n", _("String to expect in the content"));
	printf(" %s\n", "-u, --url=PATH");
	printf("    %s\n", _("URL to GET or POST (default: /)"));
	printf(" %s\n", "-P, --post=STRING");
	printf("    %s\n", _("URL decoded http POST data"));
	printf(" %s\n", "-j, --method=STRING  (for example: HEAD, OPTIONS, TRACE, PUT, DELETE, CONNECT)");
	printf("    %s\n", _("Set HTTP method."));
	printf(" %s\n", "-N, --no-body");
	printf("    %s\n", _("Don't wait for document body: stop reading after headers."));
	printf("    %s\n", _("(Note that this still does an HTTP GET or POST, not a HEAD.)"));
	printf(" %s\n", "-M, --max-age=SECONDS");
	printf("    %s\n", _("Warn if document is more than SECONDS old. the number can also be of"));
	printf("    %s\n", _("the form \"10m\" for minutes, \"10h\" for hours, or \"10d\" for days."));
	printf(" %s\n", "-T, --content-type=STRING");
	printf("    %s\n", _("specify Content-Type header media type when POSTing\n"));
	printf(" %s\n", "-l, --linespan");
	printf("    %s\n", _("Allow regex to span newlines (must precede -r or -R)"));
	printf(" %s\n", "-r, --regex, --ereg=STRING");
	printf("    %s\n", _("Search page for regex STRING"));
	printf(" %s\n", "-R, --eregi=STRING");
	printf("    %s\n", _("Search page for case-insensitive regex STRING"));
	printf(" %s\n", "--invert-regex");
	printf("    %s\n", _("Return STATE if found, OK if not (STATE is CRITICAL, per default)"));
	printf("    %s\n", _("can be changed with --state--regex)"));
	printf(" %s\n", "--state-regex=STATE");
	printf("    %s\n", _("Return STATE if regex is found, OK if not. STATE can be one of \"critical\",\"warning\""));
	printf(" %s\n", "-a, --authorization=AUTH_PAIR");
	printf("    %s\n", _("Username:password on sites with basic authentication"));
	printf(" %s\n", "-b, --proxy-authorization=AUTH_PAIR");
	printf("    %s\n", _("Username:password on proxy-servers with basic authentication"));
	printf(" %s\n", "-A, --useragent=STRING");
	printf("    %s\n", _("String to be sent in http header as \"User Agent\""));
	printf(" %s\n", "-k, --header=STRING");
	printf("    %s\n", _("Any other tags to be sent in http header. Use multiple times for additional headers"));
	printf(" %s\n", "-E, --extended-perfdata");
	printf("    %s\n", _("Print additional performance data"));
	printf(" %s\n", "-B, --show-body");
	printf("    %s\n", _("Print body content below status line"));
	printf(" %s\n", "-L, --link");
	printf("    %s\n", _("Wrap output in HTML link (obsoleted by urlize)"));
	printf(" %s\n", "-f, --onredirect=<ok|warning|critical|follow|sticky|stickyport|curl>");
	printf("    %s\n", _("How to handle redirected pages. sticky is like follow but stick to the"));
	printf("    %s\n", _("specified IP address. stickyport also ensures port stays the same."));
	printf("    %s\n", _("follow uses the old redirection algorithm of check_http."));
	printf("    %s\n", _("curl uses CURL_FOLLOWLOCATION built into libcurl."));
	printf(" %s\n", "--max-redirs=INTEGER");
	printf("    %s", _("Maximal number of redirects (default: "));
	printf("%d)\n", DEFAULT_MAX_REDIRS);
	printf(" %s\n", "-m, --pagesize=INTEGER<:INTEGER>");
	printf("    %s\n", _("Minimum page size required (bytes) : Maximum page size required (bytes)"));
	printf("\n");
	printf(" %s\n", "--http-version=VERSION");
	printf("    %s\n", _("Connect via specific HTTP protocol."));
	printf("    %s\n", _("1.0 = HTTP/1.0, 1.1 = HTTP/1.1, 2.0 = HTTP/2 (HTTP/2 will fail without -S)"));
	printf(" %s\n", "--enable-automatic-decompression");
	printf("    %s\n", _("Enable automatic decompression of body (CURLOPT_ACCEPT_ENCODING)."));
	printf(" %s\n", "--haproxy-protocol");
	printf("    %s\n", _("Send HAProxy proxy protocol v1 header (CURLOPT_HAPROXYPROTOCOL)."));
	printf(" %s\n", "--cookie-jar=FILE");
	printf("    %s\n", _("Store cookies in the cookie jar and send them out when requested."));
	printf("    %s\n", _("Specify an empty string as FILE to enable curl's cookie engine without saving"));
	printf("    %s\n", _("the cookies to disk. Only enabling the engine without saving to disk requires"));
	printf("    %s\n", _("handling multiple requests internally to curl, so use it with --onredirect=curl"));
	printf("\n");

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("This plugin will attempt to open an HTTP connection with the host."));
	printf(" %s\n", _("Successful connects return STATE_OK, refusals and timeouts return STATE_CRITICAL"));
	printf(" %s\n", _("other errors return STATE_UNKNOWN.  Successful connects, but incorrect response"));
	printf(" %s\n", _("messages from the host result in STATE_WARNING return values.  If you are"));
	printf(" %s\n", _("checking a virtual server that uses 'host headers' you must supply the FQDN"));
	printf(" %s\n", _("(fully qualified domain name) as the [host_name] argument."));

#ifdef LIBCURL_FEATURE_SSL
	printf("\n");
	printf(" %s\n", _("This plugin can also check whether an SSL enabled web server is able to"));
	printf(" %s\n", _("serve content (optionally within a specified time) or whether the X509 "));
	printf(" %s\n", _("certificate is still valid for the specified number of days."));
	printf("\n");
	printf(" %s\n", _("Please note that this plugin does not check if the presented server"));
	printf(" %s\n", _("certificate matches the hostname of the server, or if the certificate"));
	printf(" %s\n", _("has a valid chain of trust to one of the locally installed CAs."));
	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n\n", "CHECK CONTENT: check_curl -w 5 -c 10 --ssl -H www.verisign.com");
	printf(" %s\n", _("When the 'www.verisign.com' server returns its content within 5 seconds,"));
	printf(" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
	printf(" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
	printf(" %s\n", _("a STATE_CRITICAL will be returned."));
	printf("\n");
	printf(" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 14");
	printf(" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 14 days,"));
	printf(" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
	printf(" %s\n", _("14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when"));
	printf(" %s\n\n", _("the certificate is expired."));
	printf("\n");
	printf(" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 30,14");
	printf(" %s\n", _("When the certificate of 'www.verisign.com' is valid for more than 30 days,"));
	printf(" %s\n", _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
	printf(" %s\n", _("30 days, but more than 14 days, a STATE_WARNING is returned."));
	printf(" %s\n", _("A STATE_CRITICAL will be returned when certificate expires in less than 14 days"));
#endif

	printf("\n %s\n", "CHECK WEBSERVER CONTENT VIA PROXY:");
	printf(" %s\n", _("It is recommended to use an environment proxy like:"));
	printf(" %s\n", _("http_proxy=http://192.168.100.35:3128 ./check_curl -H www.monitoring-plugins.org"));
	printf(" %s\n", _("legacy proxy requests in check_http style still work:"));
	printf(" %s\n", _("check_curl -I 192.168.100.35 -p 3128 -u http://www.monitoring-plugins.org/ -H www.monitoring-plugins.org"));

#ifdef LIBCURL_FEATURE_SSL
	printf("\n %s\n", "CHECK SSL WEBSERVER CONTENT VIA PROXY USING HTTP 1.1 CONNECT: ");
	printf(" %s\n", _("It is recommended to use an environment proxy like:"));
	printf(" %s\n", _("https_proxy=http://192.168.100.35:3128 ./check_curl -H www.verisign.com -S"));
	printf(" %s\n", _("legacy proxy requests in check_http style still work:"));
	printf(" %s\n", _("check_curl -I 192.168.100.35 -p 3128 -u https://www.verisign.com/ -S -j CONNECT -H www.verisign.com "));
	printf(" %s\n", _("all these options are needed: -I <proxy> -p <proxy-port> -u <check-url> -S(sl) -j CONNECT -H <webserver>"));
	printf(" %s\n", _("a STATE_OK will be returned. When the server returns its content but exceeds"));
	printf(" %s\n", _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
	printf(" %s\n", _("a STATE_CRITICAL will be returned."));

#endif

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s -H <vhost> | -I <IP-address> [-u <uri>] [-p <port>]\n", progname);
	printf("       [-J <client certificate file>] [-K <private key>] [--ca-cert <CA certificate file>] [-D]\n");
	printf("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L] [-E] [-a auth]\n");
	printf("       [-b proxy_auth] [-f <ok|warning|critical|follow|sticky|stickyport|curl>]\n");
	printf("       [-e <expect>] [-d string] [-s string] [-l] [-r <regex> | -R <case-insensitive regex>]\n");
	printf("       [-P string] [-m <min_pg_size>:<max_pg_size>] [-4|-6] [-N] [-M <age>]\n");
	printf("       [-A string] [-k string] [-S <version>] [--sni] [--haproxy-protocol]\n");
	printf("       [-T <content-type>] [-j method]\n");
	printf("       [--http-version=<version>] [--enable-automatic-decompression]\n");
	printf("       [--cookie-jar=<cookie jar file>\n");
	printf(" %s -H <vhost> | -I <IP-address> -C <warn_age>[,<crit_age>]\n", progname);
	printf("       [-p <port>] [-t <timeout>] [-4|-6] [--sni]\n");
	printf("\n");
#ifdef LIBCURL_FEATURE_SSL
	printf("%s\n", _("In the first form, make an HTTP request."));
	printf("%s\n\n", _("In the second form, connect to the server and check the TLS certificate."));
#endif
}

void print_curl_version(void) { printf("%s\n", curl_version()); }

int curlhelp_initwritebuffer(curlhelp_write_curlbuf *buf) {
	buf->bufsize = DEFAULT_BUFFER_SIZE;
	buf->buflen = 0;
	buf->buf = (char *)malloc((size_t)buf->bufsize);
	if (buf->buf == NULL) {
		return -1;
	}
	return 0;
}

size_t curlhelp_buffer_write_callback(void *buffer, size_t size, size_t nmemb, void *stream) {
	curlhelp_write_curlbuf *buf = (curlhelp_write_curlbuf *)stream;

	while (buf->bufsize < buf->buflen + size * nmemb + 1) {
		buf->bufsize = buf->bufsize * 2;
		buf->buf = (char *)realloc(buf->buf, buf->bufsize);
		if (buf->buf == NULL) {
			fprintf(stderr, "malloc failed (%d) %s\n", errno, strerror(errno));
			return -1;
		}
	}

	memcpy(buf->buf + buf->buflen, buffer, size * nmemb);
	buf->buflen += size * nmemb;
	buf->buf[buf->buflen] = '\0';

	return (int)(size * nmemb);
}

size_t curlhelp_buffer_read_callback(void *buffer, size_t size, size_t nmemb, void *stream) {
	curlhelp_read_curlbuf *buf = (curlhelp_read_curlbuf *)stream;

	size_t n = min(nmemb * size, buf->buflen - buf->pos);

	memcpy(buffer, buf->buf + buf->pos, n);
	buf->pos += n;

	return (int)n;
}

void curlhelp_freewritebuffer(curlhelp_write_curlbuf *buf) {
	free(buf->buf);
	buf->buf = NULL;
}

int curlhelp_initreadbuffer(curlhelp_read_curlbuf *buf, const char *data, size_t datalen) {
	buf->buflen = datalen;
	buf->buf = (char *)malloc((size_t)buf->buflen);
	if (buf->buf == NULL) {
		return -1;
	}
	memcpy(buf->buf, data, datalen);
	buf->pos = 0;
	return 0;
}

void curlhelp_freereadbuffer(curlhelp_read_curlbuf *buf) {
	free(buf->buf);
	buf->buf = NULL;
}

/* TODO: where to put this, it's actually part of sstrings2 (logically)?
 */
const char *strrstr2(const char *haystack, const char *needle) {
	if (haystack == NULL || needle == NULL) {
		return NULL;
	}

	if (haystack[0] == '\0' || needle[0] == '\0') {
		return NULL;
	}

	int counter = 0;
	const char *prev_pos = NULL;
	const char *pos = haystack;
	size_t len = strlen(needle);
	for (;;) {
		pos = strstr(pos, needle);
		if (pos == NULL) {
			if (counter == 0) {
				return NULL;
			}
			return prev_pos;
		}
		counter++;
		prev_pos = pos;
		pos += len;
		if (*pos == '\0') {
			return prev_pos;
		}
	}
}

int curlhelp_parse_statusline(const char *buf, curlhelp_statusline *status_line) {
	/* find last start of a new header */
	const char *start = strrstr2(buf, "\r\nHTTP/");
	if (start != NULL) {
		start += 2;
		buf = start;
	}

	char *first_line_end = strstr(buf, "\r\n");
	if (first_line_end == NULL) {
		return -1;
	}

	size_t first_line_len = (size_t)(first_line_end - buf);
	status_line->first_line = (char *)malloc(first_line_len + 1);
	if (status_line->first_line == NULL) {
		return -1;
	}
	memcpy(status_line->first_line, buf, first_line_len);
	status_line->first_line[first_line_len] = '\0';
	char *first_line_buf = strdup(status_line->first_line);

	/* protocol and version: "HTTP/x.x" SP or "HTTP/2" SP */
	char *p = strtok(first_line_buf, "/");
	if (p == NULL) {
		free(first_line_buf);
		return -1;
	}
	if (strcmp(p, "HTTP") != 0) {
		free(first_line_buf);
		return -1;
	}

	p = strtok(NULL, " ");
	if (p == NULL) {
		free(first_line_buf);
		return -1;
	}

	char *pp;
	if (strchr(p, '.') != NULL) {

		/* HTTP 1.x case */
		strtok(p, ".");
		status_line->http_major = (int)strtol(p, &pp, 10);
		if (*pp != '\0') {
			free(first_line_buf);
			return -1;
		}
		strtok(NULL, " ");
		status_line->http_minor = (int)strtol(p, &pp, 10);
		if (*pp != '\0') {
			free(first_line_buf);
			return -1;
		}
		p += 4; /* 1.x SP */
	} else {
		/* HTTP 2 case */
		status_line->http_major = (int)strtol(p, &pp, 10);
		status_line->http_minor = 0;
		p += 2; /* 2 SP */
	}

	/* status code: "404" or "404.1", then SP */

	p = strtok(p, " ");
	if (p == NULL) {
		free(first_line_buf);
		return -1;
	}
	if (strchr(p, '.') != NULL) {
		char *ppp;
		ppp = strtok(p, ".");
		status_line->http_code = (int)strtol(ppp, &pp, 10);
		if (*pp != '\0') {
			free(first_line_buf);
			return -1;
		}
		ppp = strtok(NULL, "");
		status_line->http_subcode = (int)strtol(ppp, &pp, 10);
		if (*pp != '\0') {
			free(first_line_buf);
			return -1;
		}
		p += 6; /* 400.1 SP */
	} else {
		status_line->http_code = (int)strtol(p, &pp, 10);
		status_line->http_subcode = -1;
		if (*pp != '\0') {
			free(first_line_buf);
			return -1;
		}
		p += 4; /* 400 SP */
	}

	/* Human readable message: "Not Found" CRLF */

	p = strtok(p, "");
	if (p == NULL) {
		status_line->msg = "";
		return 0;
	}
	status_line->msg = status_line->first_line + (p - first_line_buf);
	free(first_line_buf);

	return 0;
}

void curlhelp_free_statusline(curlhelp_statusline *status_line) { free(status_line->first_line); }

char *get_header_value(const struct phr_header *headers, const size_t nof_headers, const char *header) {
	for (size_t i = 0; i < nof_headers; i++) {
		if (headers[i].name != NULL && strncasecmp(header, headers[i].name, max(headers[i].name_len, 4)) == 0) {
			return strndup(headers[i].value, headers[i].value_len);
		}
	}
	return NULL;
}

int check_document_dates(const curlhelp_write_curlbuf *header_buf, char (*msg)[DEFAULT_BUFFER_SIZE], int maximum_age) {
	struct phr_header headers[255];
	size_t nof_headers = 255;
	curlhelp_statusline status_line;
	size_t msglen;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major, &status_line.http_minor,
								 &status_line.http_code, &status_line.msg, &msglen, headers, &nof_headers, 0);

	if (res == -1) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Failed to parse Response\n"));
	}

	char *server_date = get_header_value(headers, nof_headers, "date");
	char *document_date = get_header_value(headers, nof_headers, "last-modified");

	int date_result = STATE_OK;
	if (!server_date || !*server_date) {
		char tmp[DEFAULT_BUFFER_SIZE];

		snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sServer date unknown, "), *msg);
		strcpy(*msg, tmp);

		date_result = max_state_alt(STATE_UNKNOWN, date_result);

	} else if (!document_date || !*document_date) {
		char tmp[DEFAULT_BUFFER_SIZE];

		snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sDocument modification date unknown, "), *msg);
		strcpy(*msg, tmp);

		date_result = max_state_alt(STATE_CRITICAL, date_result);

	} else {
		time_t srv_data = curl_getdate(server_date, NULL);
		time_t doc_data = curl_getdate(document_date, NULL);
		if (verbose >= 2) {
			printf("* server date: '%s' (%d), doc_date: '%s' (%d)\n", server_date, (int)srv_data, document_date, (int)doc_data);
		}
		if (srv_data <= 0) {
			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sServer date \"%100s\" unparsable, "), *msg, server_date);
			strcpy(*msg, tmp);

			date_result = max_state_alt(STATE_CRITICAL, date_result);
		} else if (doc_data <= 0) {
			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sDocument date \"%100s\" unparsable, "), *msg, document_date);
			strcpy(*msg, tmp);

			date_result = max_state_alt(STATE_CRITICAL, date_result);
		} else if (doc_data > srv_data + 30) {
			char tmp[DEFAULT_BUFFER_SIZE];

			snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sDocument is %d seconds in the future, "), *msg, (int)doc_data - (int)srv_data);
			strcpy(*msg, tmp);

			date_result = max_state_alt(STATE_CRITICAL, date_result);
		} else if (doc_data < srv_data - maximum_age) {
			int n = (srv_data - doc_data);
			if (n > (60 * 60 * 24 * 2)) {
				char tmp[DEFAULT_BUFFER_SIZE];

				snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sLast modified %.1f days ago, "), *msg, ((float)n) / (60 * 60 * 24));
				strcpy(*msg, tmp);

				date_result = max_state_alt(STATE_CRITICAL, date_result);
			} else {
				char tmp[DEFAULT_BUFFER_SIZE];

				snprintf(tmp, DEFAULT_BUFFER_SIZE, _("%sLast modified %d:%02d:%02d ago, "), *msg, n / (60 * 60), (n / 60) % 60, n % 60);
				strcpy(*msg, tmp);

				date_result = max_state_alt(STATE_CRITICAL, date_result);
			}
		}
	}

	if (server_date) {
		free(server_date);
	}
	if (document_date) {
		free(document_date);
	}

	return date_result;
}

int get_content_length(const curlhelp_write_curlbuf *header_buf, const curlhelp_write_curlbuf *body_buf) {
	struct phr_header headers[255];
	size_t nof_headers = 255;
	size_t msglen;
	curlhelp_statusline status_line;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major, &status_line.http_minor,
								 &status_line.http_code, &status_line.msg, &msglen, headers, &nof_headers, 0);

	if (res == -1) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Failed to parse Response\n"));
	}

	char *content_length_s = get_header_value(headers, nof_headers, "content-length");
	if (!content_length_s) {
		return header_buf->buflen + body_buf->buflen;
	}

	content_length_s += strspn(content_length_s, " \t");
	size_t content_length = atoi(content_length_s);
	if (content_length != body_buf->buflen) {
		/* TODO: should we warn if the actual and the reported body length don't match? */
	}

	if (content_length_s) {
		free(content_length_s);
	}

	return header_buf->buflen + body_buf->buflen;
}

/* TODO: is there a better way in libcurl to check for the SSL library? */
curlhelp_ssl_library curlhelp_get_ssl_library(void) {
	curlhelp_ssl_library ssl_library = CURLHELP_SSL_LIBRARY_UNKNOWN;

	curl_version_info_data *version_data = curl_version_info(CURLVERSION_NOW);
	if (version_data == NULL) {
		return CURLHELP_SSL_LIBRARY_UNKNOWN;
	}

	char *ssl_version = strdup(version_data->ssl_version);
	if (ssl_version == NULL) {
		return CURLHELP_SSL_LIBRARY_UNKNOWN;
	}

	char *library = strtok(ssl_version, "/");
	if (library == NULL) {
		return CURLHELP_SSL_LIBRARY_UNKNOWN;
	}

	if (strcmp(library, "OpenSSL") == 0) {
		ssl_library = CURLHELP_SSL_LIBRARY_OPENSSL;
	} else if (strcmp(library, "LibreSSL") == 0) {
		ssl_library = CURLHELP_SSL_LIBRARY_LIBRESSL;
	} else if (strcmp(library, "GnuTLS") == 0) {
		ssl_library = CURLHELP_SSL_LIBRARY_GNUTLS;
	} else if (strcmp(library, "NSS") == 0) {
		ssl_library = CURLHELP_SSL_LIBRARY_NSS;
	}

	if (verbose >= 2) {
		printf("* SSL library string is : %s %s (%d)\n", version_data->ssl_version, library, ssl_library);
	}

	free(ssl_version);

	return ssl_library;
}

const char *curlhelp_get_ssl_library_string(curlhelp_ssl_library ssl_library) {
	switch (ssl_library) {
	case CURLHELP_SSL_LIBRARY_OPENSSL:
		return "OpenSSL";
	case CURLHELP_SSL_LIBRARY_LIBRESSL:
		return "LibreSSL";
	case CURLHELP_SSL_LIBRARY_GNUTLS:
		return "GnuTLS";
	case CURLHELP_SSL_LIBRARY_NSS:
		return "NSS";
	case CURLHELP_SSL_LIBRARY_UNKNOWN:
	default:
		return "unknown";
	}
}

#ifdef LIBCURL_FEATURE_SSL
#	ifndef USE_OPENSSL
time_t parse_cert_date(const char *s) {
	if (!s) {
		return -1;
	}

	/* Jan 17 14:25:12 2020 GMT */
	struct tm tm;
	char *res = strptime(s, "%Y-%m-%d %H:%M:%S GMT", &tm);
	/* Sep 11 12:00:00 2020 GMT */
	if (res == NULL) {
		strptime(s, "%Y %m %d %H:%M:%S GMT", &tm);
	}
	time_t date = mktime(&tm);

	return date;
}

/* TODO: this needs cleanup in the sslutils.c, maybe we the #else case to
 * OpenSSL could be this function
 */
int net_noopenssl_check_certificate(cert_ptr_union *cert_ptr, int days_till_exp_warn, int days_till_exp_crit) {
	struct curl_slist *slist;
	int cname_found = 0;
	char *start_date_str = NULL;
	char *end_date_str = NULL;
	time_t start_date;
	time_t end_date;
	char *tz;
	float time_left;
	int days_left;
	int time_remaining;
	char timestamp[50] = "";
	int status = STATE_UNKNOWN;

	if (verbose >= 2) {
		printf("**** REQUEST CERTIFICATES ****\n");
	}

	for (int i = 0; i < cert_ptr->to_certinfo->num_of_certs; i++) {
		for (slist = cert_ptr->to_certinfo->certinfo[i]; slist; slist = slist->next) {
			/* find first common name in subject,
			 * TODO: check alternative subjects for
			 * TODO: have a decent parser here and not a hack
			 * multi-host certificate, check wildcards
			 */
			if (strncasecmp(slist->data, "Subject:", 8) == 0) {
				int d = 3;
				char *p = strstr(slist->data, "CN=");
				if (p == NULL) {
					d = 5;
					p = strstr(slist->data, "CN = ");
				}
				if (p != NULL) {
					if (strncmp(host_name, p + d, strlen(host_name)) == 0) {
						cname_found = 1;
					}
				}
			} else if (strncasecmp(slist->data, "Start Date:", 11) == 0) {
				start_date_str = &slist->data[11];
			} else if (strncasecmp(slist->data, "Expire Date:", 12) == 0) {
				end_date_str = &slist->data[12];
			} else if (strncasecmp(slist->data, "Cert:", 5) == 0) {
				goto HAVE_FIRST_CERT;
			}
			if (verbose >= 2) {
				printf("%d ** %s\n", i, slist->data);
			}
		}
	}
HAVE_FIRST_CERT:

	if (verbose >= 2) {
		printf("**** REQUEST CERTIFICATES ****\n");
	}

	if (!cname_found) {
		printf("%s\n", _("CRITICAL - Cannot retrieve certificate subject."));
		return STATE_CRITICAL;
	}

	start_date = parse_cert_date(start_date_str);
	if (start_date <= 0) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, _("WARNING - Unparsable 'Start Date' in certificate: '%s'"), start_date_str);
		puts(msg);
		return STATE_WARNING;
	}

	end_date = parse_cert_date(end_date_str);
	if (end_date <= 0) {
		snprintf(msg, DEFAULT_BUFFER_SIZE, _("WARNING - Unparsable 'Expire Date' in certificate: '%s'"), start_date_str);
		puts(msg);
		return STATE_WARNING;
	}

	time_left = difftime(end_date, time(NULL));
	days_left = time_left / 86400;
	tz = getenv("TZ");
	setenv("TZ", "GMT", 1);
	tzset();
	strftime(timestamp, 50, "%c %z", localtime(&end_date));
	if (tz) {
		setenv("TZ", tz, 1);
	} else {
		unsetenv("TZ");
	}
	tzset();

	if (days_left > 0 && days_left <= days_till_exp_warn) {
		printf(_("%s - Certificate '%s' expires in %d day(s) (%s).\n"), (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL",
			   host_name, days_left, timestamp);
		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else if (days_left == 0 && time_left > 0) {
		if (time_left >= 3600) {
			time_remaining = (int)time_left / 3600;
		} else {
			time_remaining = (int)time_left / 60;
		}

		printf(_("%s - Certificate '%s' expires in %u %s (%s)\n"), (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", host_name,
			   time_remaining, time_left >= 3600 ? "hours" : "minutes", timestamp);

		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else if (time_left < 0) {
		printf(_("CRITICAL - Certificate '%s' expired on %s.\n"), host_name, timestamp);
		status = STATE_CRITICAL;
	} else if (days_left == 0) {
		printf(_("%s - Certificate '%s' just expired (%s).\n"), (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", host_name,
			   timestamp);
		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else {
		printf(_("OK - Certificate '%s' will expire on %s.\n"), host_name, timestamp);
		status = STATE_OK;
	}
	return status;
}
#	endif /* USE_OPENSSL */
#endif     /* LIBCURL_FEATURE_SSL */
