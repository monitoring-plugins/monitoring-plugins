#include "./check_curl_helpers.h"
#include <stdbool.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include "../utils.h"
#include "check_curl.d/config.h"
#include "output.h"
#include "perfdata.h"
#include "states.h"

extern int verbose;
char errbuf[MAX_INPUT_BUFFER];
bool is_openssl_callback = false;
bool add_sslctx_verify_fun = false;

check_curl_configure_curl_wrapper
check_curl_configure_curl(const check_curl_static_curl_config config,
						  check_curl_working_state working_state, bool check_cert,
						  bool on_redirect_dependent, int follow_method, int max_depth) {
	check_curl_configure_curl_wrapper result = {
		.errorcode = OK,
		.curl_state =
			{
				.curl_global_initialized = false,
				.curl_easy_initialized = false,
				.curl = NULL,

				.body_buf_initialized = false,
				.body_buf = NULL,
				.header_buf_initialized = false,
				.header_buf = NULL,
				.status_line_initialized = false,
				.status_line = NULL,
				.put_buf_initialized = false,
				.put_buf = NULL,

				.header_list = NULL,
				.host = NULL,
			},
	};

	if ((result.curl_state.status_line = calloc(1, sizeof(curlhelp_statusline))) == NULL) {
		die(STATE_UNKNOWN, "HTTP UNKNOWN - allocation of statusline failed\n");
	}

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
		die(STATE_UNKNOWN, "HTTP UNKNOWN - curl_global_init failed\n");
	}
	result.curl_state.curl_global_initialized = true;

	if ((result.curl_state.curl = curl_easy_init()) == NULL) {
		die(STATE_UNKNOWN, "HTTP UNKNOWN - curl_easy_init failed\n");
	}
	result.curl_state.curl_easy_initialized = true;

	if (verbose >= 1) {
		handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_VERBOSE, 1),
									   "CURLOPT_VERBOSE");
	}

	/* print everything on stdout like check_http would do */
	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_STDERR, stdout),
								   "CURLOPT_STDERR");

	if (config.automatic_decompression) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 21, 6)
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_ACCEPT_ENCODING, ""),
			"CURLOPT_ACCEPT_ENCODING");
#else
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_ENCODING, ""), "CURLOPT_ENCODING");
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 21, 6) */
	}

	/* initialize buffer for body of the answer */
	if (curlhelp_initwritebuffer(&result.curl_state.body_buf) < 0) {
		die(STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for body\n");
	}
	result.curl_state.body_buf_initialized = true;

	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_WRITEFUNCTION,
													curlhelp_buffer_write_callback),
								   "CURLOPT_WRITEFUNCTION");
	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_WRITEDATA,
													(void *)result.curl_state.body_buf),
								   "CURLOPT_WRITEDATA");

	/* initialize buffer for header of the answer */
	if (curlhelp_initwritebuffer(&result.curl_state.header_buf) < 0) {
		die(STATE_UNKNOWN, "HTTP CRITICAL - out of memory allocating buffer for header\n");
	}
	result.curl_state.header_buf_initialized = true;

	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_HEADERFUNCTION,
													curlhelp_buffer_write_callback),
								   "CURLOPT_HEADERFUNCTION");
	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_WRITEHEADER,
													(void *)result.curl_state.header_buf),
								   "CURLOPT_WRITEHEADER");

	/* set the error buffer */
	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_ERRORBUFFER, errbuf),
		"CURLOPT_ERRORBUFFER");

	/* set timeouts */
	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_CONNECTTIMEOUT, config.socket_timeout),
		"CURLOPT_CONNECTTIMEOUT");

	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_TIMEOUT, config.socket_timeout),
		"CURLOPT_TIMEOUT");

	/* enable haproxy protocol */
	if (config.haproxy_protocol) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_HAPROXYPROTOCOL, 1L),
			"CURLOPT_HAPROXYPROTOCOL");
	}

	// fill dns resolve cache to make curl connect to the given server_address instead of the
	// host_name, only required for ssl, because we use the host_name later on to make SNI happy
	char dnscache[DEFAULT_BUFFER_SIZE];
	char addrstr[DEFAULT_BUFFER_SIZE / 2];
	if (working_state.use_ssl && working_state.host_name != NULL) {
		char *tmp_mod_address;

		/* lookup_host() requires an IPv6 address without the brackets. */
		if ((strnlen(working_state.server_address, MAX_IPV4_HOSTLENGTH) > 2) &&
			(working_state.server_address[0] == '[')) {
			// Duplicate and strip the leading '['
			tmp_mod_address =
				strndup(working_state.server_address + 1, strlen(working_state.server_address) - 2);
		} else {
			tmp_mod_address = working_state.server_address;
		}

		int res;
		if ((res = lookup_host(tmp_mod_address, addrstr, DEFAULT_BUFFER_SIZE / 2,
							   config.sin_family)) != 0) {
			die(STATE_CRITICAL,
				_("Unable to lookup IP address for '%s': getaddrinfo returned %d - %s"),
				working_state.server_address, res, gai_strerror(res));
		}

		snprintf(dnscache, DEFAULT_BUFFER_SIZE, "%s:%d:%s", working_state.host_name,
				 working_state.serverPort, addrstr);
		result.curl_state.host = curl_slist_append(NULL, dnscache);
		curl_easy_setopt(result.curl_state.curl, CURLOPT_RESOLVE, result.curl_state.host);

		if (verbose >= 1) {
			printf("* curl CURLOPT_RESOLVE: %s\n", dnscache);
		}
	}

	// If server_address is an IPv6 address it must be surround by square brackets
	struct in6_addr tmp_in_addr;
	if (inet_pton(AF_INET6, working_state.server_address, &tmp_in_addr) == 1) {
		char *new_server_address = calloc(strlen(working_state.server_address) + 3, sizeof(char));
		if (new_server_address == NULL) {
			die(STATE_UNKNOWN, "HTTP UNKNOWN - Unable to allocate memory\n");
		}
		snprintf(new_server_address, strlen(working_state.server_address) + 3, "[%s]",
				 working_state.server_address);
		working_state.server_address = new_server_address;
	}

	/* compose URL: use the address we want to connect to, set Host: header later */
	char *url = fmt_url(working_state);

	if (verbose >= 1) {
		printf("* curl CURLOPT_URL: %s\n", url);
	}
	handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_URL, url),
								   "CURLOPT_URL");

	free(url);

	/* extract proxy information for legacy proxy https requests */
	if (!strcmp(working_state.http_method, "CONNECT") ||
		strstr(working_state.server_url, "http") == working_state.server_url) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_PROXY, working_state.server_address),
			"CURLOPT_PROXY");
		handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_PROXYPORT,
														(long)working_state.serverPort),
									   "CURLOPT_PROXYPORT");
		if (verbose >= 2) {
			printf("* curl CURLOPT_PROXY: %s:%d\n", working_state.server_address,
				   working_state.serverPort);
		}
		working_state.http_method = "GET";
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_URL, working_state.server_url),
			"CURLOPT_URL");
	}

	/* disable body for HEAD request */
	if (working_state.http_method && !strcmp(working_state.http_method, "HEAD")) {
		working_state.no_body = true;
	}

	/* set HTTP protocol version */
	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_HTTP_VERSION, config.curl_http_version),
		"CURLOPT_HTTP_VERSION");

	/* set HTTP method */
	if (working_state.http_method) {
		if (!strcmp(working_state.http_method, "POST")) {
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_POST, 1), "CURLOPT_POST");
		} else if (!strcmp(working_state.http_method, "PUT")) {
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_UPLOAD, 1), "CURLOPT_UPLOAD");
		} else {
			handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl,
															CURLOPT_CUSTOMREQUEST,
															working_state.http_method),
										   "CURLOPT_CUSTOMREQUEST");
		}
	}

	char *force_host_header = NULL;
	/* check if Host header is explicitly set in options */
	if (config.http_opt_headers_count) {
		for (size_t i = 0; i < config.http_opt_headers_count; i++) {
			if (strncmp(config.http_opt_headers[i], "Host:", 5) == 0) {
				force_host_header = config.http_opt_headers[i];
			}
		}
	}

	/* set hostname (virtual hosts), not needed if CURLOPT_CONNECT_TO is used, but left in
	 * anyway */
	char http_header[DEFAULT_BUFFER_SIZE];
	if (working_state.host_name != NULL && force_host_header == NULL) {
		if ((working_state.virtualPort != HTTP_PORT && !working_state.use_ssl) ||
			(working_state.virtualPort != HTTPS_PORT && working_state.use_ssl)) {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s:%d", working_state.host_name,
					 working_state.virtualPort);
		} else {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Host: %s", working_state.host_name);
		}
		result.curl_state.header_list =
			curl_slist_append(result.curl_state.header_list, http_header);
	}

	/* always close connection, be nice to servers */
	snprintf(http_header, DEFAULT_BUFFER_SIZE, "Connection: close");
	result.curl_state.header_list = curl_slist_append(result.curl_state.header_list, http_header);

	/* attach additional headers supplied by the user */
	/* optionally send any other header tag */
	if (config.http_opt_headers_count) {
		for (size_t i = 0; i < config.http_opt_headers_count; i++) {
			result.curl_state.header_list =
				curl_slist_append(result.curl_state.header_list, config.http_opt_headers[i]);
		}
	}

	/* set HTTP headers */
	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_HTTPHEADER, result.curl_state.header_list),
		"CURLOPT_HTTPHEADER");

#ifdef LIBCURL_FEATURE_SSL
	/* set SSL version, warn about insecure or unsupported versions */
	if (working_state.use_ssl) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSLVERSION, config.ssl_version),
			"CURLOPT_SSLVERSION");
	}

	/* client certificate and key to present to server (SSL) */
	if (config.client_cert) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSLCERT, config.client_cert),
			"CURLOPT_SSLCERT");
	}

	if (config.client_privkey) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSLKEY, config.client_privkey),
			"CURLOPT_SSLKEY");
	}

	if (config.ca_cert) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_CAINFO, config.ca_cert),
			"CURLOPT_CAINFO");
	}

	if (config.ca_cert || config.verify_peer_and_host) {
		/* per default if we have a CA verify both the peer and the
		 * hostname in the certificate, can be switched off later */
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_VERIFYPEER, 1),
			"CURLOPT_SSL_VERIFYPEER");
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_VERIFYHOST, 2),
			"CURLOPT_SSL_VERIFYHOST");
	} else {
		/* backward-compatible behaviour, be tolerant in checks
		 * TODO: depending on more options have aspects we want
		 * to be less tolerant about ssl verfications
		 */
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_VERIFYPEER, 0),
			"CURLOPT_SSL_VERIFYPEER");
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_VERIFYHOST, 0),
			"CURLOPT_SSL_VERIFYHOST");
	}

	/* detect SSL library used by libcurl */
	curlhelp_ssl_library ssl_library = curlhelp_get_ssl_library();

	/* try hard to get a stack of certificates to verify against */
	if (check_cert) {
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
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
			break;

		case CURLHELP_SSL_LIBRARY_NSS:
#		if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
			/* NSS: support for CERTINFO is implemented since 7.34.0 */
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
#		else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			die(STATE_CRITICAL,
				"HTTP CRITICAL - Cannot retrieve certificates (libcurl linked with SSL library "
				"'%s' is too old)\n",
				curlhelp_get_ssl_library_string(ssl_library));
#		endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			break;

		case CURLHELP_SSL_LIBRARY_GNUTLS:
#		if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0)
			/* GnuTLS: support for CERTINFO is implemented since 7.42.0 */
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_CERTINFO, 1L), "CURLOPT_CERTINFO");
#		else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0) */
			die(STATE_CRITICAL,
				"HTTP CRITICAL - Cannot retrieve certificates (libcurl linked with SSL library "
				"'%s' is too old)\n",
				curlhelp_get_ssl_library_string(ssl_library));
#		endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 42, 0) */
			break;

		case CURLHELP_SSL_LIBRARY_UNKNOWN:
		default:
			die(STATE_CRITICAL,
				"HTTP CRITICAL - Cannot retrieve certificates (unknown SSL library '%s', must "
				"implement first)\n",
				curlhelp_get_ssl_library_string(ssl_library));
			break;
		}
#	else  /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */
		/* old libcurl, our only hope is OpenSSL, otherwise we are out of luck */
		if (ssl_library == CURLHELP_SSL_LIBRARY_OPENSSL ||
			ssl_library == CURLHELP_SSL_LIBRARY_LIBRESSL) {
			add_sslctx_verify_fun = true;
		} else {
			die(STATE_CRITICAL, "HTTP CRITICAL - Cannot retrieve certificates (no "
								"CURLOPT_SSL_CTX_FUNCTION, no OpenSSL library or libcurl "
								"too old and has no CURLOPT_CERTINFO)\n");
		}
#	endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 1) */
	}

#	if LIBCURL_VERSION_NUM >=                                                                     \
		MAKE_LIBCURL_VERSION(7, 10, 6) /* required for CURLOPT_SSL_CTX_FUNCTION */
	// ssl ctx function is not available with all ssl backends
	if (curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_CTX_FUNCTION, NULL) !=
		CURLE_UNKNOWN_OPTION) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_SSL_CTX_FUNCTION, sslctxfun),
			"CURLOPT_SSL_CTX_FUNCTION");
	}
#	endif
#endif /* LIBCURL_FEATURE_SSL */

	/* set default or user-given user agent identification */
	handle_curl_option_return_code(
		curl_easy_setopt(result.curl_state.curl, CURLOPT_USERAGENT, config.user_agent),
		"CURLOPT_USERAGENT");

	/* proxy-authentication */
	if (strcmp(config.proxy_auth, "")) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_PROXYUSERPWD, config.proxy_auth),
			"CURLOPT_PROXYUSERPWD");
	}

	/* authentication */
	if (strcmp(config.user_auth, "")) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_USERPWD, config.user_auth),
			"CURLOPT_USERPWD");
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
	 * handle_curl_option_return_code (curl_easy_setopt( curl, CURLOPT_HTTPAUTH,
	 * (long)CURLAUTH_DIGEST ), "CURLOPT_HTTPAUTH");
	 */

	/* handle redirections */
	if (on_redirect_dependent) {
		if (follow_method == FOLLOW_LIBCURL) {
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_FOLLOWLOCATION, 1),
				"CURLOPT_FOLLOWLOCATION");

			/* default -1 is infinite, not good, could lead to zombie plugins!
			   Setting it to one bigger than maximal limit to handle errors nicely below
			 */
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_MAXREDIRS, max_depth + 1),
				"CURLOPT_MAXREDIRS");

			/* for now allow only http and https (we are a http(s) check plugin in the end) */
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 85, 0)
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https"),
				"CURLOPT_REDIR_PROTOCOLS_STR");
#elif LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 19, 4)
			handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl,
															CURLOPT_REDIR_PROTOCOLS,
															CURLPROTO_HTTP | CURLPROTO_HTTPS),
										   "CURLOPT_REDIRECT_PROTOCOLS");
#endif

			/* TODO: handle the following aspects of redirection, make them
			 * command line options too later:
			  CURLOPT_POSTREDIR: method switch
			  CURLINFO_REDIRECT_URL: custom redirect option
			  CURLOPT_REDIRECT_PROTOCOLS: allow people to step outside safe protocols
			  CURLINFO_REDIRECT_COUNT: get the number of redirects, print it, maybe a range
			 option here is nice like for expected page size?
			*/
		} else {
			/* old style redirection*/
		}
	}
	/* no-body */
	if (working_state.no_body) {
		handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl, CURLOPT_NOBODY, 1),
									   "CURLOPT_NOBODY");
	}

	/* IPv4 or IPv6 forced DNS resolution */
	if (config.sin_family == AF_UNSPEC) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER),
			"CURLOPT_IPRESOLVE(CURL_IPRESOLVE_WHATEVER)");
	} else if (config.sin_family == AF_INET) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4),
			"CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V4)");
	}
#if defined(USE_IPV6) && defined(LIBCURL_FEATURE_IPV6)
	else if (config.sin_family == AF_INET6) {
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6),
			"CURLOPT_IPRESOLVE(CURL_IPRESOLVE_V6)");
	}
#endif

	/* either send http POST data (any data, not only POST)*/
	if (!strcmp(working_state.http_method, "POST") || !strcmp(working_state.http_method, "PUT")) {
		/* set content of payload for POST and PUT */
		if (config.http_content_type) {
			snprintf(http_header, DEFAULT_BUFFER_SIZE, "Content-Type: %s",
					 config.http_content_type);
			result.curl_state.header_list =
				curl_slist_append(result.curl_state.header_list, http_header);
		}
		/* NULL indicates "HTTP Continue" in libcurl, provide an empty string
		 * in case of no POST/PUT data */
		if (!working_state.http_post_data) {
			working_state.http_post_data = "";
		}

		if (!strcmp(working_state.http_method, "POST")) {
			/* POST method, set payload with CURLOPT_POSTFIELDS */
			handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl,
															CURLOPT_POSTFIELDS,
															working_state.http_post_data),
										   "CURLOPT_POSTFIELDS");
		} else if (!strcmp(working_state.http_method, "PUT")) {
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_READFUNCTION,
								 (curl_read_callback)curlhelp_buffer_read_callback),
				"CURLOPT_READFUNCTION");
			if (curlhelp_initreadbuffer(&result.curl_state.put_buf, working_state.http_post_data,
										strlen(working_state.http_post_data)) < 0) {
				die(STATE_UNKNOWN,
					"HTTP CRITICAL - out of memory allocating read buffer for PUT\n");
			}
			result.curl_state.put_buf_initialized = true;
			handle_curl_option_return_code(curl_easy_setopt(result.curl_state.curl,
															CURLOPT_READDATA,
															(void *)result.curl_state.put_buf),
										   "CURLOPT_READDATA");
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_INFILESIZE,
								 (curl_off_t)strlen(working_state.http_post_data)),
				"CURLOPT_INFILESIZE");
		}
	}

	/* cookie handling */
	if (config.cookie_jar_file != NULL) {
		/* enable reading cookies from a file, and if the filename is an empty string, only
		 * enable the curl cookie engine */
		handle_curl_option_return_code(
			curl_easy_setopt(result.curl_state.curl, CURLOPT_COOKIEFILE, config.cookie_jar_file),
			"CURLOPT_COOKIEFILE");
		/* now enable saving cookies to a file, but only if the filename is not an empty string,
		 * since writing it would fail */
		if (*config.cookie_jar_file) {
			handle_curl_option_return_code(
				curl_easy_setopt(result.curl_state.curl, CURLOPT_COOKIEJAR, config.cookie_jar_file),
				"CURLOPT_COOKIEJAR");
		}
	}

	result.working_state = working_state;

	return result;
}

void handle_curl_option_return_code(CURLcode res, const char *option) {
	if (res != CURLE_OK) {
		die(STATE_CRITICAL, _("Error while setting cURL option '%s': cURL returned %d - %s"),
			option, res, curl_easy_strerror(res));
	}
}

char *get_header_value(const struct phr_header *headers, const size_t nof_headers,
					   const char *header) {
	for (size_t i = 0; i < nof_headers; i++) {
		if (headers[i].name != NULL &&
			strncasecmp(header, headers[i].name, max(headers[i].name_len, 4)) == 0) {
			return strndup(headers[i].value, headers[i].value_len);
		}
	}
	return NULL;
}

check_curl_working_state check_curl_working_state_init() {
	check_curl_working_state result = {
		.server_address = NULL,
		.server_url = DEFAULT_SERVER_URL,
		.host_name = NULL,
		.http_method = NULL,
		.http_post_data = NULL,
		.virtualPort = 0,
		.serverPort = HTTP_PORT,
		.use_ssl = false,
		.no_body = false,
	};
	return result;
}

check_curl_config check_curl_config_init() {
	check_curl_config tmp = {
		.initial_config = check_curl_working_state_init(),

		.curl_config =
			{
				.automatic_decompression = false,
				.socket_timeout = DEFAULT_SOCKET_TIMEOUT,
				.haproxy_protocol = false,
				.sin_family = AF_UNSPEC,
				.curl_http_version = CURL_HTTP_VERSION_NONE,
				.http_opt_headers = NULL,
				.http_opt_headers_count = 0,
				.ssl_version = CURL_SSLVERSION_DEFAULT,
				.client_cert = NULL,
				.client_privkey = NULL,
				.ca_cert = NULL,
				.verify_peer_and_host = false,
				.user_agent = {'\0'},
				.proxy_auth = "",
				.user_auth = "",
				.http_content_type = NULL,
				.cookie_jar_file = NULL,
			},
		.max_depth = DEFAULT_MAX_REDIRS,
		.followmethod = FOLLOW_HTTP_CURL,
		.followsticky = STICKY_NONE,

		.maximum_age = -1,
		.regexp = {},
		.compiled_regex = {},
		.state_regex = STATE_CRITICAL,
		.invert_regex = false,
		.check_cert = false,
		.continue_after_check_cert = false,
		.days_till_exp_warn = 0,
		.days_till_exp_crit = 0,
		.thlds = mp_thresholds_init(),
		.page_length_limits = mp_range_init(),
		.page_length_limits_is_set = false,
		.server_expect =
			{
				.string = HTTP_EXPECT,
				.is_present = false,
			},
		.string_expect = "",
		.header_expect = "",
		.on_redirect_result_state = STATE_OK,
		.on_redirect_dependent = false,

		.show_extended_perfdata = false,
		.show_body = false,

		.output_format_is_set = false,
	};

	snprintf(tmp.curl_config.user_agent, DEFAULT_BUFFER_SIZE, "%s/v%s (monitoring-plugins %s, %s)",
			 "check_curl", NP_VERSION, VERSION, curl_version());

	return tmp;
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
		printf("* SSL library string is : %s %s (%d)\n", version_data->ssl_version, library,
			   ssl_library);
	}

	free(ssl_version);

	return ssl_library;
}

const char *curlhelp_get_ssl_library_string(const curlhelp_ssl_library ssl_library) {
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

size_t get_content_length(const curlhelp_write_curlbuf *header_buf,
						  const curlhelp_write_curlbuf *body_buf) {
	struct phr_header headers[255];
	size_t nof_headers = 255;
	size_t msglen;
	curlhelp_statusline status_line;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major,
								 &status_line.http_minor, &status_line.http_code, &status_line.msg,
								 &msglen, headers, &nof_headers, 0);

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

mp_subcheck check_document_dates(const curlhelp_write_curlbuf *header_buf, const int maximum_age) {
	struct phr_header headers[255];
	size_t nof_headers = 255;
	curlhelp_statusline status_line;
	size_t msglen;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major,
								 &status_line.http_minor, &status_line.http_code, &status_line.msg,
								 &msglen, headers, &nof_headers, 0);

	if (res == -1) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Failed to parse Response\n"));
	}

	char *server_date = get_header_value(headers, nof_headers, "date");
	char *document_date = get_header_value(headers, nof_headers, "last-modified");

	mp_subcheck sc_document_dates = mp_subcheck_init();
	if (!server_date || !*server_date) {
		xasprintf(&sc_document_dates.output, _("Server date unknown"));
		sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_UNKNOWN);
	} else if (!document_date || !*document_date) {
		xasprintf(&sc_document_dates.output, _("Document modification date unknown, "));
		sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
	} else {
		time_t srv_data = curl_getdate(server_date, NULL);
		time_t doc_data = curl_getdate(document_date, NULL);

		if (verbose >= 2) {
			printf("* server date: '%s' (%d), doc_date: '%s' (%d)\n", server_date, (int)srv_data,
				   document_date, (int)doc_data);
		}

		if (srv_data <= 0) {
			xasprintf(&sc_document_dates.output, _("Server date \"%100s\" unparsable"),
					  server_date);
			sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
		} else if (doc_data <= 0) {

			xasprintf(&sc_document_dates.output, _("Document date \"%100s\" unparsable"),
					  document_date);
			sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
		} else if (doc_data > srv_data + 30) {

			xasprintf(&sc_document_dates.output, _("Document is %d seconds in the future"),
					  (int)doc_data - (int)srv_data);

			sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
		} else if (doc_data < srv_data - maximum_age) {
			time_t last_modified = (srv_data - doc_data);
			if (last_modified > (60 * 60 * 24 * 2)) { // two days hardcoded?
				xasprintf(&sc_document_dates.output, _("Last modified %.1f days ago"),
						  ((float)last_modified) / (60 * 60 * 24));
				sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
			} else {
				xasprintf(&sc_document_dates.output, _("Last modified %ld:%02ld:%02ld ago"),
						  last_modified / (60 * 60), (last_modified / 60) % 60, last_modified % 60);
				sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_CRITICAL);
			}
		} else {
			// TODO is this the OK case?
			time_t last_modified = (srv_data - doc_data);
			xasprintf(&sc_document_dates.output, _("Last modified %ld:%02ld:%02ld ago"),
					  last_modified / (60 * 60), (last_modified / 60) % 60, last_modified % 60);
			sc_document_dates = mp_set_subcheck_state(sc_document_dates, STATE_OK);
		}
	}

	if (server_date) {
		free(server_date);
	}
	if (document_date) {
		free(document_date);
	}

	return sc_document_dates;
}

void curlhelp_free_statusline(curlhelp_statusline *status_line) { free(status_line->first_line); }

int curlhelp_parse_statusline(const char *buf, curlhelp_statusline *status_line) {
	/* find last start of a new header */
	const char *start = strrstr2(buf, "\r\nHTTP/");
	if (start != NULL) {
		start += 2;
		buf = start;
	}

	// Accept either LF or CRLF as end of line for the status line
	// CRLF is the standard (RFC9112), but it is recommended to accept both
	size_t length_of_first_line = strcspn(buf, "\r\n");
	const char *first_line_end = &buf[length_of_first_line];
	if (first_line_end == NULL) {
		return -1;
	}

	size_t first_line_len = (size_t)(first_line_end - buf);
	status_line->first_line = (char *)calloc(first_line_len + 1, sizeof(char));
	if (status_line->first_line == NULL) {
		return -1;
	}

	memcpy(status_line->first_line, buf, first_line_len);
	status_line->first_line[first_line_len] = '\0';
	char *first_line_buf = strdup(status_line->first_line);

	/* protocol and version: "HTTP/x.x" SP or "HTTP/2" SP */
	char *temp_string = strtok(first_line_buf, "/");
	if (temp_string == NULL) {
		if (verbose > 1) {
			printf("%s: no / found\n", __func__);
		}
		free(first_line_buf);
		return -1;
	}

	if (strcmp(temp_string, "HTTP") != 0) {
		if (verbose > 1) {
			printf("%s: string 'HTTP' not found\n", __func__);
		}
		free(first_line_buf);
		return -1;
	}

	// try to find a space in the remaining string?
	// the space after HTTP/1.1 probably
	temp_string = strtok(NULL, " ");
	if (temp_string == NULL) {
		if (verbose > 1) {
			printf("%s: no space after protocol definition\n", __func__);
		}
		free(first_line_buf);
		return -1;
	}

	char *temp_string_2;
	if (strchr(temp_string, '.') != NULL) {
		/* HTTP 1.x case */
		strtok(temp_string, ".");
		status_line->http_major = (int)strtol(temp_string, &temp_string_2, 10);
		if (*temp_string_2 != '\0') {
			free(first_line_buf);
			return -1;
		}
		strtok(NULL, " ");
		status_line->http_minor = (int)strtol(temp_string, &temp_string_2, 10);
		if (*temp_string_2 != '\0') {
			free(first_line_buf);
			return -1;
		}
		temp_string += 4; /* 1.x SP */
	} else {
		/* HTTP 2 case */
		status_line->http_major = (int)strtol(temp_string, &temp_string_2, 10);
		status_line->http_minor = 0;
		temp_string += 2; /* 2 SP */
	}

	/* status code: "404" or "404.1", then SP */
	temp_string = strtok(temp_string, " ");
	if (temp_string == NULL) {
		free(first_line_buf);
		return -1;
	}
	if (strchr(temp_string, '.') != NULL) {
		char *ppp;
		ppp = strtok(temp_string, ".");
		status_line->http_code = (int)strtol(ppp, &temp_string_2, 10);
		if (*temp_string_2 != '\0') {
			free(first_line_buf);
			return -1;
		}
		ppp = strtok(NULL, "");
		status_line->http_subcode = (int)strtol(ppp, &temp_string_2, 10);
		if (*temp_string_2 != '\0') {
			free(first_line_buf);
			return -1;
		}
		temp_string += 6; /* 400.1 SP */
	} else {
		status_line->http_code = (int)strtol(temp_string, &temp_string_2, 10);
		status_line->http_subcode = -1;
		if (*temp_string_2 != '\0') {
			free(first_line_buf);
			return -1;
		}
		temp_string += 4; /* 400 SP */
	}

	/* Human readable message: "Not Found" CRLF */

	temp_string = strtok(temp_string, "");
	if (temp_string == NULL) {
		status_line->msg = "";
		return 0;
	}
	status_line->msg = status_line->first_line + (temp_string - first_line_buf);
	free(first_line_buf);

	return 0;
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

void curlhelp_freereadbuffer(curlhelp_read_curlbuf *buf) {
	free(buf->buf);
	buf->buf = NULL;
}

void curlhelp_freewritebuffer(curlhelp_write_curlbuf *buf) {
	free(buf->buf);
	buf->buf = NULL;
}

int curlhelp_initreadbuffer(curlhelp_read_curlbuf **buf, const char *data, size_t datalen) {
	if ((*buf = calloc(1, sizeof(curlhelp_read_curlbuf))) == NULL) {
		return 1;
	}

	(*buf)->buflen = datalen;
	(*buf)->buf = (char *)calloc((*buf)->buflen, sizeof(char));
	if ((*buf)->buf == NULL) {
		return -1;
	}
	memcpy((*buf)->buf, data, datalen);
	(*buf)->pos = 0;
	return 0;
}

size_t curlhelp_buffer_read_callback(void *buffer, size_t size, size_t nmemb, void *stream) {
	curlhelp_read_curlbuf *buf = (curlhelp_read_curlbuf *)stream;

	size_t minimalSize = min(nmemb * size, buf->buflen - buf->pos);

	memcpy(buffer, buf->buf + buf->pos, minimalSize);
	buf->pos += minimalSize;

	return minimalSize;
}

int curlhelp_initwritebuffer(curlhelp_write_curlbuf **buf) {
	if ((*buf = calloc(1, sizeof(curlhelp_write_curlbuf))) == NULL) {
		return 1;
	}
	(*buf)->bufsize = DEFAULT_BUFFER_SIZE * sizeof(char);
	(*buf)->buflen = 0;
	(*buf)->buf = (char *)calloc((*buf)->bufsize, sizeof(char));
	if ((*buf)->buf == NULL) {
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
			return 0;
		}
	}

	memcpy(buf->buf + buf->buflen, buffer, size * nmemb);
	buf->buflen += size * nmemb;
	buf->buf[buf->buflen] = '\0';

	return size * nmemb;
}

void cleanup(check_curl_global_state global_state) {
	if (global_state.status_line_initialized) {
		curlhelp_free_statusline(global_state.status_line);
	}
	global_state.status_line_initialized = false;

	if (global_state.curl_easy_initialized) {
		curl_easy_cleanup(global_state.curl);
	}
	global_state.curl_easy_initialized = false;

	if (global_state.curl_global_initialized) {
		curl_global_cleanup();
	}
	global_state.curl_global_initialized = false;

	if (global_state.body_buf_initialized) {
		curlhelp_freewritebuffer(global_state.body_buf);
	}
	global_state.body_buf_initialized = false;

	if (global_state.header_buf_initialized) {
		curlhelp_freewritebuffer(global_state.header_buf);
	}
	global_state.header_buf_initialized = false;

	if (global_state.put_buf_initialized) {
		curlhelp_freereadbuffer(global_state.put_buf);
	}
	global_state.put_buf_initialized = false;

	if (global_state.header_list) {
		curl_slist_free_all(global_state.header_list);
	}

	if (global_state.host) {
		curl_slist_free_all(global_state.host);
	}
}

int lookup_host(const char *host, char *buf, size_t buflen, sa_family_t addr_family) {
	struct addrinfo hints = {
		.ai_family = addr_family,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_CANONNAME,
	};

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
			printf("* getaddrinfo IPv%d address: %s\n", res->ai_family == PF_INET6 ? 6 : 4,
				   addrstr);
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

/* Checks if the server 'reply' is one of the expected 'statuscodes' */
bool expected_statuscode(const char *reply, const char *statuscodes) {
	char *expected;

	if ((expected = strdup(statuscodes)) == NULL) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Memory allocation error\n"));
	}

	bool result = false;
	for (char *code = strtok(expected, ","); code != NULL; code = strtok(NULL, ",")) {
		if (strstr(reply, code) != NULL) {
			result = true;
			break;
		}
	}

	free(expected);
	return result;
}

/* returns a string "HTTP/1.x" or "HTTP/2" */
char *string_statuscode(int major, int minor) {
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

/* check whether a file exists */
void test_file(char *path) {
	if (access(path, R_OK) == 0) {
		return;
	}
	usage2(_("file does not exist or is not readable"), path);
}

mp_subcheck mp_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn,
										 int days_till_exp_crit);

mp_subcheck check_curl_certificate_checks(CURL *curl, X509 *cert, int warn_days_till_exp,
										  int crit_days_till_exp) {
	mp_subcheck sc_cert_result = mp_subcheck_init();
	sc_cert_result = mp_set_subcheck_default_state(sc_cert_result, STATE_OK);

#ifdef LIBCURL_FEATURE_SSL
	if (is_openssl_callback) {
#	ifdef USE_OPENSSL
		/* check certificate with OpenSSL functions, curl has been built against OpenSSL
		 * and we actually have OpenSSL in the monitoring tools
		 */
		return mp_net_ssl_check_certificate(cert, warn_days_till_exp, crit_days_till_exp);
#	else  /* USE_OPENSSL */
		xasprintf(&result.output, "HTTP CRITICAL - Cannot retrieve certificates - OpenSSL "
								  "callback used and not linked against OpenSSL\n");
		mp_set_subcheck_state(result, STATE_CRITICAL);
#	endif /* USE_OPENSSL */
	} else {
		struct curl_slist *slist;

		cert_ptr_union cert_ptr = {0};
		cert_ptr.to_info = NULL;
		CURLcode res = curl_easy_getinfo(curl, CURLINFO_CERTINFO, &cert_ptr.to_info);
		if (!res && cert_ptr.to_info) {
#	ifdef USE_OPENSSL
			/* We have no OpenSSL in libcurl, but we can use OpenSSL for X509 cert
			 * parsing We only check the first certificate and assume it's the one of
			 * the server
			 */
			char *raw_cert = NULL;
			bool got_first_cert = false;
			for (int i = 0; i < cert_ptr.to_certinfo->num_of_certs; i++) {
				if (got_first_cert) {
					break;
				}

				for (slist = cert_ptr.to_certinfo->certinfo[i]; slist; slist = slist->next) {
					if (verbose >= 2) {
						printf("%d ** %s\n", i, slist->data);
					}
					if (strncmp(slist->data, "Cert:", 5) == 0) {
						raw_cert = &slist->data[5];
						got_first_cert = true;
						break;
					}
				}
			}

			if (!raw_cert) {

				xasprintf(&sc_cert_result.output,
						  _("Cannot retrieve certificates from CERTINFO information - "
							"certificate data was empty"));
				sc_cert_result = mp_set_subcheck_state(sc_cert_result, STATE_CRITICAL);
				return sc_cert_result;
			}

			BIO *cert_BIO = BIO_new(BIO_s_mem());
			BIO_write(cert_BIO, raw_cert, (int)strlen(raw_cert));

			cert = PEM_read_bio_X509(cert_BIO, NULL, NULL, NULL);
			if (!cert) {
				xasprintf(&sc_cert_result.output,
						  _("Cannot read certificate from CERTINFO information - BIO error"));
				sc_cert_result = mp_set_subcheck_state(sc_cert_result, STATE_CRITICAL);
				return sc_cert_result;
			}

			BIO_free(cert_BIO);
			return mp_net_ssl_check_certificate(cert, warn_days_till_exp, crit_days_till_exp);
#	else  /* USE_OPENSSL */
			/* We assume we don't have OpenSSL and np_net_ssl_check_certificate at our
			 * disposal, so we use the libcurl CURLINFO data
			 */
			return net_noopenssl_check_certificate(&cert_ptr, days_till_exp_warn,
												   days_till_exp_crit);
#	endif /* USE_OPENSSL */
		} else {
			xasprintf(&sc_cert_result.output,
					  _("Cannot retrieve certificates - cURL returned %d - %s"), res,
					  curl_easy_strerror(res));
			mp_set_subcheck_state(sc_cert_result, STATE_CRITICAL);
		}
	}
#endif /* LIBCURL_FEATURE_SSL */

	return sc_cert_result;
}

char *fmt_url(check_curl_working_state workingState) {
	char *url = calloc(DEFAULT_BUFFER_SIZE, sizeof(char));
	if (url == NULL) {
		die(STATE_UNKNOWN, "memory allocation failed");
	}

	snprintf(url, DEFAULT_BUFFER_SIZE, "%s://%s:%d%s", workingState.use_ssl ? "https" : "http",
			 (workingState.use_ssl & (workingState.host_name != NULL))
				 ? workingState.host_name
				 : workingState.server_address,
			 workingState.serverPort, workingState.server_url);

	return url;
}
