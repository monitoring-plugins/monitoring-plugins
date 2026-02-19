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
#include "output.h"
#include "perfdata.h"

#include <assert.h>
#include "common.h"
#include "utils.h"
#include "./check_curl.d/check_curl_helpers.h"

#ifndef LIBCURL_PROTOCOL_HTTP
#	error libcurl compiled without HTTP support, compiling check_curl plugin does not makes a lot of sense
#endif

#include "curl/curl.h"
#include "curl/easy.h"

#include "uriparser/Uri.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
#	include <openssl/opensslv.h>
#endif

#include <netdb.h>

enum {
	REGS = 2,
};

#include "regex.h"

// Globals
int verbose = 0;

extern char errbuf[MAX_INPUT_BUFFER];
extern bool is_openssl_callback;
extern bool add_sslctx_verify_fun;

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
static X509 *cert = NULL;
#endif /* defined(HAVE_SSL) && defined(USE_OPENSSL) */

typedef struct {
	int errorcode;
	check_curl_config config;
} check_curl_config_wrapper;
static check_curl_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);

static mp_subcheck check_http(check_curl_config /*config*/, check_curl_working_state workingState,
							  long redir_depth);

typedef struct {
	long redir_depth;
	check_curl_working_state working_state;
	int error_code;
	check_curl_global_state curl_state;
} redir_wrapper;
static redir_wrapper redir(curlhelp_write_curlbuf * /*header_buf*/, check_curl_config /*config*/,
						   long redir_depth, check_curl_working_state working_state);

static void print_help(void);
void print_usage(void);

static void print_curl_version(void);

// typedef struct {
// 	int errorcode;
// } check_curl_evaluation_wrapper;
// check_curl_evaluation_wrapper check_curl_evaluate(check_curl_config config,
// 												  mp_check overall[static 1]) {}

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
mp_state_enum np_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn,
										   int days_till_exp_crit);
#endif /* defined(HAVE_SSL) && defined(USE_OPENSSL) */

int main(int argc, char **argv) {
#ifdef __OpenBSD__
	/* - rpath is required to read --extra-opts, CA and/or client certs
	 * - wpath is required to write --cookie-jar (possibly given up later)
	 * - inet is required for sockets
	 * - dns is required for name lookups */
	pledge("stdio rpath wpath inet dns", NULL);
#endif // __OpenBSD__

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

#ifdef __OpenBSD__
	if (!config.curl_config.cookie_jar_file) {
		if (verbose >= 2) {
			printf(_("* No \"--cookie-jar\" is used, giving up \"wpath\" pledge(2)\n"));
		}
		pledge("stdio rpath inet dns", NULL);
	}
#endif // __OpenBSD__

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	check_curl_working_state working_state = config.initial_config;

	mp_check overall = mp_check_init();
	mp_subcheck sc_test = check_http(config, working_state, 0);

	mp_add_subcheck_to_check(&overall, sc_test);

	mp_exit(overall);
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
		printf("*   issuer:\n");
		X509_NAME *issuer = X509_get_issuer_name(cert);
		X509_NAME_print_ex_fp(stdout, issuer, 5, XN_FLAG_MULTILINE);
		printf("* curl verify_callback:\n*   subject:\n");
		X509_NAME *subject = X509_get_subject_name(cert);
		X509_NAME_print_ex_fp(stdout, subject, 5, XN_FLAG_MULTILINE);
		puts("");
	}
	return 1;
}
#	endif /* USE_OPENSSL */
#endif     /* HAVE_SSL */

#ifdef HAVE_SSL
#	ifdef USE_OPENSSL
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

mp_subcheck check_http(const check_curl_config config, check_curl_working_state workingState,
					   long redir_depth) {

	// =======================
	// Initialisation for curl
	// =======================
	check_curl_configure_curl_wrapper conf_curl_struct = check_curl_configure_curl(
		config.curl_config, workingState, config.check_cert, config.on_redirect_dependent,
		config.followmethod, config.max_depth);

	check_curl_global_state curl_state = conf_curl_struct.curl_state;
	workingState = conf_curl_struct.working_state;

	mp_subcheck sc_result = mp_subcheck_init();

	char *url = fmt_url(workingState);
	xasprintf(&sc_result.output, "Testing %s", url);
	// TODO add some output here URL or something
	free(url);

	// ==============
	// do the request
	// ==============
	CURLcode res = curl_easy_perform(curl_state.curl);

	if (verbose >= 2 && workingState.http_post_data) {
		printf("**** REQUEST CONTENT ****\n%s\n", workingState.http_post_data);
	}

	mp_subcheck sc_curl = mp_subcheck_init();

	/* Curl errors, result in critical Nagios state */
	if (res != CURLE_OK) {
		xasprintf(&sc_curl.output, _("Error while performing connection: cURL returned %d - %s"),
				  res, errbuf[0] ? errbuf : curl_easy_strerror(res));
		sc_curl = mp_set_subcheck_state(sc_curl, STATE_CRITICAL);
		mp_add_subcheck_to_subcheck(&sc_result, sc_curl);
		return sc_result;
	}

	/* get status line of answer, check sanity of HTTP code */
	if (curlhelp_parse_statusline(curl_state.header_buf->buf, curl_state.status_line) < 0) {
		sc_result = mp_set_subcheck_state(sc_result, STATE_CRITICAL);
		/* we cannot know the major/minor version here for sure as we cannot parse the first
		 * line */
		xasprintf(&sc_result.output, "HTTP/x.x unknown - Unparsable status line");
		return sc_result;
	}

	curl_state.status_line_initialized = true;

	size_t page_len = get_content_length(curl_state.header_buf, curl_state.body_buf);

	double total_time;
	handle_curl_option_return_code(
		curl_easy_getinfo(curl_state.curl, CURLINFO_TOTAL_TIME, &total_time),
		"CURLINFO_TOTAL_TIME");

	xasprintf(
		&sc_curl.output, "%s %d %s - %ld bytes in %.3f second response time",
		string_statuscode(curl_state.status_line->http_major, curl_state.status_line->http_minor),
		curl_state.status_line->http_code, curl_state.status_line->msg, page_len, total_time);
	sc_curl = mp_set_subcheck_state(sc_curl, STATE_OK);
	mp_add_subcheck_to_subcheck(&sc_result, sc_curl);

	// ==========
	// Evaluation
	// ==========

#ifdef LIBCURL_FEATURE_SSL
	if (workingState.use_ssl && config.check_cert) {
		mp_subcheck sc_certificate = check_curl_certificate_checks(
			curl_state.curl, cert, config.days_till_exp_warn, config.days_till_exp_crit);

		mp_add_subcheck_to_subcheck(&sc_result, sc_certificate);
		if (!config.continue_after_check_cert) {
			return sc_result;
		}
	}
#endif

	/* we got the data and we executed the request in a given time, so we can append
	 * performance data to the answer always
	 */

	// total time the query took
	mp_perfdata pd_total_time = perfdata_init();
	mp_perfdata_value pd_val_total_time = mp_create_pd_value(total_time);
	pd_total_time.value = pd_val_total_time;
	pd_total_time = mp_pd_set_thresholds(pd_total_time, config.thlds);
	pd_total_time.label = "time";
	pd_total_time.uom = "s";

	mp_subcheck sc_total_time = mp_subcheck_init();
	sc_total_time = mp_set_subcheck_state(sc_total_time, mp_get_pd_status(pd_total_time));
	xasprintf(&sc_total_time.output, "Total connection time: %fs", total_time);
	mp_add_perfdata_to_subcheck(&sc_total_time, pd_total_time);

	mp_add_subcheck_to_subcheck(&sc_result, sc_total_time);

	if (config.show_extended_perfdata) {
		// overall connection time
		mp_perfdata pd_time_connect = perfdata_init();
		double time_connect;
		handle_curl_option_return_code(
			curl_easy_getinfo(curl_state.curl, CURLINFO_CONNECT_TIME, &time_connect),
			"CURLINFO_CONNECT_TIME");

		mp_perfdata_value pd_val_time_connect = mp_create_pd_value(time_connect);
		pd_time_connect.value = pd_val_time_connect;
		pd_time_connect.label = "time_connect";
		pd_time_connect.uom = "s";
		pd_time_connect = mp_set_pd_max_value(
			pd_time_connect, mp_create_pd_value(config.curl_config.socket_timeout));

		pd_time_connect = mp_pd_set_thresholds(pd_time_connect, config.thlds);
		mp_add_perfdata_to_subcheck(&sc_result, pd_time_connect);

		// application connection time, used to compute other timings
		double time_appconnect;
		handle_curl_option_return_code(
			curl_easy_getinfo(curl_state.curl, CURLINFO_APPCONNECT_TIME, &time_appconnect),
			"CURLINFO_APPCONNECT_TIME");

		if (workingState.use_ssl) {
			mp_perfdata pd_time_tls = perfdata_init();
			{
				mp_perfdata_value pd_val_time_tls =
					mp_create_pd_value(time_appconnect - time_connect);

				pd_time_tls.value = pd_val_time_tls;
			}
			pd_time_tls.label = "time_tls";
			pd_time_tls.uom = "s";
			mp_add_perfdata_to_subcheck(&sc_result, pd_time_tls);
		}

		mp_perfdata pd_time_headers = perfdata_init();
		{
			double time_headers;
			handle_curl_option_return_code(
				curl_easy_getinfo(curl_state.curl, CURLINFO_PRETRANSFER_TIME, &time_headers),
				"CURLINFO_PRETRANSFER_TIME");

			mp_perfdata_value pd_val_time_headers =
				mp_create_pd_value(time_headers - time_appconnect);

			pd_time_headers.value = pd_val_time_headers;
		}
		pd_time_headers.label = "time_headers";
		pd_time_headers.uom = "s";
		mp_add_perfdata_to_subcheck(&sc_result, pd_time_headers);

		mp_perfdata pd_time_firstbyte = perfdata_init();
		double time_firstbyte;
		handle_curl_option_return_code(
			curl_easy_getinfo(curl_state.curl, CURLINFO_STARTTRANSFER_TIME, &time_firstbyte),
			"CURLINFO_STARTTRANSFER_TIME");

		mp_perfdata_value pd_val_time_firstbyte = mp_create_pd_value(time_firstbyte);
		pd_time_firstbyte.value = pd_val_time_firstbyte;
		pd_time_firstbyte.label = "time_firstbyte";
		pd_time_firstbyte.uom = "s";
		mp_add_perfdata_to_subcheck(&sc_result, pd_time_firstbyte);

		mp_perfdata pd_time_transfer = perfdata_init();
		pd_time_transfer.value = mp_create_pd_value(total_time - time_firstbyte);
		pd_time_transfer.label = "time_transfer";
		pd_time_transfer.uom = "s";
		mp_add_perfdata_to_subcheck(&sc_result, pd_time_transfer);
	}

	/* return a CRITICAL status if we couldn't read any data */
	if (strlen(curl_state.header_buf->buf) == 0 && strlen(curl_state.body_buf->buf) == 0) {
		sc_result = mp_set_subcheck_state(sc_result, STATE_CRITICAL);
		xasprintf(&sc_result.output, "No header received from host");
		return sc_result;
	}

	/* get result code from cURL */
	long httpReturnCode;
	handle_curl_option_return_code(
		curl_easy_getinfo(curl_state.curl, CURLINFO_RESPONSE_CODE, &httpReturnCode),
		"CURLINFO_RESPONSE_CODE");
	if (verbose >= 2) {
		printf("* curl CURLINFO_RESPONSE_CODE is %ld\n", httpReturnCode);
	}

	/* print status line, header, body if verbose */
	if (verbose >= 2) {
		printf("**** HEADER ****\n%s\n**** CONTENT ****\n%s\n", curl_state.header_buf->buf,
			   (workingState.no_body ? "  [[ skipped ]]" : curl_state.body_buf->buf));
	}

	/* make sure the status line matches the response we are looking for */
	mp_subcheck sc_expect = mp_subcheck_init();
	sc_expect = mp_set_subcheck_default_state(sc_expect, STATE_OK);
	if (!expected_statuscode(curl_state.status_line->first_line, config.server_expect.string)) {
		if (workingState.serverPort == HTTP_PORT) {
			xasprintf(&sc_expect.output, _("Invalid HTTP response received from host: %s\n"),
					  curl_state.status_line->first_line);
		} else {
			xasprintf(&sc_expect.output,
					  _("Invalid HTTP response received from host on port %d: %s\n"),
					  workingState.serverPort, curl_state.status_line->first_line);
		}
		sc_expect = mp_set_subcheck_default_state(sc_expect, STATE_CRITICAL);
	} else {
		xasprintf(&sc_expect.output, _("Status line output matched \"%s\""),
				  config.server_expect.string);
	}
	mp_add_subcheck_to_subcheck(&sc_result, sc_expect);

	if (!config.server_expect.is_present) {
		/* illegal return codes result in a critical state */
		mp_subcheck sc_return_code = mp_subcheck_init();
		sc_return_code = mp_set_subcheck_default_state(sc_return_code, STATE_OK);
		xasprintf(&sc_return_code.output, "HTTP return code: %d",
				  curl_state.status_line->http_code);

		if (httpReturnCode >= 600 || httpReturnCode < 100) {
			sc_return_code = mp_set_subcheck_state(sc_return_code, STATE_CRITICAL);
			xasprintf(&sc_return_code.output, _("Invalid Status (%d, %.40s)"),
					  curl_state.status_line->http_code, curl_state.status_line->msg);
			mp_add_subcheck_to_subcheck(&sc_result, sc_return_code);
			return sc_result;
		}

		// server errors result in a critical state
		if (httpReturnCode >= 500) {
			sc_return_code = mp_set_subcheck_state(sc_return_code, STATE_CRITICAL);
			/* client errors result in a warning state */
		} else if (httpReturnCode >= 400) {
			sc_return_code = mp_set_subcheck_state(sc_return_code, STATE_WARNING);
			/* check redirected page if specified */
		} else if (httpReturnCode >= 300) {
			if (config.on_redirect_dependent) {
				if (config.followmethod == FOLLOW_LIBCURL) {
					httpReturnCode = curl_state.status_line->http_code;
					handle_curl_option_return_code(
						curl_easy_getinfo(curl_state.curl, CURLINFO_REDIRECT_COUNT, &redir_depth),
						"CURLINFO_REDIRECT_COUNT");

					if (verbose >= 2) {
						printf(_("* curl LIBINFO_REDIRECT_COUNT is %ld\n"), redir_depth);
					}

					mp_subcheck sc_redir_depth = mp_subcheck_init();
					if (redir_depth > config.max_depth) {
						xasprintf(&sc_redir_depth.output,
								  "maximum redirection depth %ld exceeded in libcurl",
								  config.max_depth);
						sc_redir_depth = mp_set_subcheck_state(sc_redir_depth, STATE_CRITICAL);
						mp_add_subcheck_to_subcheck(&sc_result, sc_redir_depth);
						return sc_result;
					}
					xasprintf(&sc_redir_depth.output, "redirection depth %ld (of a maximum %ld)",
							  redir_depth, config.max_depth);
					mp_add_subcheck_to_subcheck(&sc_result, sc_redir_depth);

				} else {
					/* old check_http style redirection, if we come
					 * back here, we are in the same status as with
					 * the libcurl method
					 */
					redir_wrapper redir_result =
						redir(curl_state.header_buf, config, redir_depth, workingState);
					cleanup(curl_state);
					mp_subcheck sc_redir =
						check_http(config, redir_result.working_state, redir_result.redir_depth);
					mp_add_subcheck_to_subcheck(&sc_result, sc_redir);

					return sc_result;
				}
			} else {
				/* this is a specific code in the command line to
				 * be returned when a redirection is encountered
				 */
				sc_return_code =
					mp_set_subcheck_state(sc_return_code, config.on_redirect_result_state);
			}
		} else {
			sc_return_code = mp_set_subcheck_state(sc_return_code, STATE_OK);
		}

		mp_add_subcheck_to_subcheck(&sc_result, sc_return_code);
	}

	/* check status codes, set exit status accordingly */
	if (curl_state.status_line->http_code != httpReturnCode) {
		mp_subcheck sc_http_return_code_sanity = mp_subcheck_init();
		sc_http_return_code_sanity =
			mp_set_subcheck_state(sc_http_return_code_sanity, STATE_CRITICAL);
		xasprintf(&sc_http_return_code_sanity.output,
				  _("HTTP CRITICAL %s %d %s - different HTTP codes (cUrl has %ld)\n"),
				  string_statuscode(curl_state.status_line->http_major,
									curl_state.status_line->http_minor),
				  curl_state.status_line->http_code, curl_state.status_line->msg, httpReturnCode);

		mp_add_subcheck_to_subcheck(&sc_result, sc_http_return_code_sanity);
		return sc_result;
	}

	if (config.maximum_age >= 0) {
		mp_subcheck sc_max_age = check_document_dates(curl_state.header_buf, config.maximum_age);
		mp_add_subcheck_to_subcheck(&sc_result, sc_max_age);
	}

	/* Page and Header content checks go here */
	if (strlen(config.header_expect)) {
		mp_subcheck sc_header_expect = mp_subcheck_init();
		sc_header_expect = mp_set_subcheck_default_state(sc_header_expect, STATE_OK);
		xasprintf(&sc_header_expect.output, "Expect %s in header", config.header_expect);

		if (!strstr(curl_state.header_buf->buf, config.header_expect)) {
			char output_header_search[30] = "";
			strncpy(&output_header_search[0], config.header_expect, sizeof(output_header_search));

			if (output_header_search[sizeof(output_header_search) - 1] != '\0') {
				bcopy("...", &output_header_search[sizeof(output_header_search) - 4], 4);
			}

			xasprintf(&sc_header_expect.output, _("header '%s' not found on '%s://%s:%d%s', "),
					  output_header_search, workingState.use_ssl ? "https" : "http",
					  workingState.host_name ? workingState.host_name : workingState.server_address,
					  workingState.serverPort, workingState.server_url);

			sc_header_expect = mp_set_subcheck_state(sc_header_expect, STATE_CRITICAL);
		}

		mp_add_subcheck_to_subcheck(&sc_result, sc_header_expect);
	}

	if (strlen(config.string_expect)) {
		mp_subcheck sc_string_expect = mp_subcheck_init();
		sc_string_expect = mp_set_subcheck_default_state(sc_string_expect, STATE_OK);
		xasprintf(&sc_string_expect.output, "Expect string \"%s\" in body", config.string_expect);

		if (!strstr(curl_state.body_buf->buf, config.string_expect)) {
			char output_string_search[30] = "";
			strncpy(&output_string_search[0], config.string_expect, sizeof(output_string_search));

			if (output_string_search[sizeof(output_string_search) - 1] != '\0') {
				bcopy("...", &output_string_search[sizeof(output_string_search) - 4], 4);
			}

			xasprintf(&sc_string_expect.output, _("string '%s' not found on '%s://%s:%d%s', "),
					  output_string_search, workingState.use_ssl ? "https" : "http",
					  workingState.host_name ? workingState.host_name : workingState.server_address,
					  workingState.serverPort, workingState.server_url);

			sc_string_expect = mp_set_subcheck_state(sc_string_expect, STATE_CRITICAL);
		}

		mp_add_subcheck_to_subcheck(&sc_result, sc_string_expect);
	}

	if (strlen(config.regexp)) {
		mp_subcheck sc_body_regex = mp_subcheck_init();
		xasprintf(&sc_body_regex.output, "Regex \"%s\" in body matched", config.regexp);
		regmatch_t pmatch[REGS];

		int errcode = regexec(&config.compiled_regex, curl_state.body_buf->buf, REGS, pmatch, 0);

		if (errcode == 0) {
			// got a match
			if (config.invert_regex) {
				sc_body_regex = mp_set_subcheck_state(sc_body_regex, config.state_regex);
			} else {
				sc_body_regex = mp_set_subcheck_state(sc_body_regex, STATE_OK);
			}
		} else if (errcode == REG_NOMATCH) {
			// got no match
			xasprintf(&sc_body_regex.output, "%s not", sc_body_regex.output);

			if (config.invert_regex) {
				sc_body_regex = mp_set_subcheck_state(sc_body_regex, STATE_OK);
			} else {
				sc_body_regex = mp_set_subcheck_state(sc_body_regex, config.state_regex);
			}
		} else {
			// error in regexec
			char error_buffer[DEFAULT_BUFFER_SIZE];
			regerror(errcode, &config.compiled_regex, &error_buffer[0], DEFAULT_BUFFER_SIZE);
			xasprintf(&sc_body_regex.output, "regexec error: %s", error_buffer);
			sc_body_regex = mp_set_subcheck_state(sc_body_regex, STATE_UNKNOWN);
		}

		mp_add_subcheck_to_subcheck(&sc_result, sc_body_regex);
	}

	// size a.k.a. page length
	mp_perfdata pd_page_length = perfdata_init();
	mp_perfdata_value pd_val_page_length = mp_create_pd_value(page_len);
	pd_page_length.value = pd_val_page_length;
	pd_page_length.label = "size";
	pd_page_length.uom = "B";
	pd_page_length.min = mp_create_pd_value(0);
	pd_page_length.warn = config.page_length_limits;
	pd_page_length.warn_present = true;

	/* make sure the page is of an appropriate size */
	if (config.page_length_limits_is_set) {
		mp_thresholds page_length_threshold = mp_thresholds_init();
		page_length_threshold.warning = config.page_length_limits;
		page_length_threshold.warning_is_set = true;

		pd_page_length = mp_pd_set_thresholds(pd_page_length, page_length_threshold);

		mp_subcheck sc_page_length = mp_subcheck_init();

		mp_add_perfdata_to_subcheck(&sc_page_length, pd_page_length);

		mp_state_enum tmp_state = mp_get_pd_status(pd_page_length);
		sc_page_length = mp_set_subcheck_state(sc_page_length, tmp_state);

		switch (tmp_state) {
		case STATE_CRITICAL:
		case STATE_WARNING:
			xasprintf(&sc_page_length.output, _("page size %zu violates threshold"), page_len);
			break;
		case STATE_OK:
			xasprintf(&sc_page_length.output, _("page size %zu is OK"), page_len);
			break;
		default:
			assert(false);
		}

		mp_add_subcheck_to_subcheck(&sc_result, sc_page_length);
	}

	return sc_result;
}

int uri_strcmp(const UriTextRangeA range, const char *stringToCompare) {
	if (!range.first) {
		return -1;
	}
	if ((size_t)(range.afterLast - range.first) < strlen(stringToCompare)) {
		return -1;
	}
	return strncmp(stringToCompare, range.first,
				   min((size_t)(range.afterLast - range.first), strlen(stringToCompare)));
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

redir_wrapper redir(curlhelp_write_curlbuf *header_buf, const check_curl_config config,
					long redir_depth, check_curl_working_state working_state) {
	curlhelp_statusline status_line;
	struct phr_header headers[255];
	size_t msglen;
	size_t nof_headers = 255;
	int res = phr_parse_response(header_buf->buf, header_buf->buflen, &status_line.http_major,
								 &status_line.http_minor, &status_line.http_code, &status_line.msg,
								 &msglen, headers, &nof_headers, 0);

	if (res == -1) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Failed to parse Response\n"));
	}

	char *location = get_header_value(headers, nof_headers, "location");

	if (location == NULL) {
		// location header not found
		die(STATE_UNKNOWN, "HTTP UNKNOWN - could not find \"location\" header\n");
	}

	if (verbose >= 2) {
		printf(_("* Seen redirect location %s\n"), location);
	}

	if (++redir_depth > config.max_depth) {
		die(STATE_WARNING, _("HTTP WARNING - maximum redirection depth %ld exceeded - %s\n"),
			config.max_depth, location);
	}

	UriParserStateA state;
	UriUriA uri;
	state.uri = &uri;
	if (uriParseUriA(&state, location) != URI_SUCCESS) {
		if (state.errorCode == URI_ERROR_SYNTAX) {
			die(STATE_UNKNOWN, _("HTTP UNKNOWN - Could not parse redirect location '%s'\n"),
				location);
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
			for (UriPathSegmentA *path_segment = uri.pathHead; path_segment;
				 path_segment = path_segment->next) {
				printf("/%s", uri_string(path_segment->text, buf, DEFAULT_BUFFER_SIZE));
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
		working_state.use_ssl = (bool)(!uri_strcmp(uri.scheme, "https"));
	}

	/* we do a sloppy test here only, because uriparser would have failed
	 * above, if the port would be invalid, we just check for MAX_PORT
	 */
	int new_port;
	if (uri.portText.first) {
		new_port = atoi(uri_string(uri.portText, buf, DEFAULT_BUFFER_SIZE));
	} else {
		new_port = HTTP_PORT;
		if (working_state.use_ssl) {
			new_port = HTTPS_PORT;
		}
	}
	if (new_port > MAX_PORT) {
		die(STATE_UNKNOWN, _("HTTP UNKNOWN - Redirection to port above %d - %s\n"), MAX_PORT,
			location);
	}

	/* by RFC 7231 relative URLs in Location should be taken relative to
	 * the original URL, so we try to form a new absolute URL here
	 */
	char *new_host;
	if (!uri.scheme.first && !uri.hostText.first) {
		new_host = strdup(working_state.host_name ? working_state.host_name
												  : working_state.server_address);
		new_port = working_state.serverPort;
		if (working_state.use_ssl) {
			uri_string(uri.scheme, "https", DEFAULT_BUFFER_SIZE);
		}
	} else {
		new_host = strdup(uri_string(uri.hostText, buf, DEFAULT_BUFFER_SIZE));
	}

	/* compose new path */
	/* TODO: handle fragments of URL */
	char *new_url = (char *)calloc(1, DEFAULT_BUFFER_SIZE);
	if (uri.pathHead) {
		for (UriPathSegmentA *pathSegment = uri.pathHead; pathSegment;
			 pathSegment = pathSegment->next) {
			strncat(new_url, "/", DEFAULT_BUFFER_SIZE);
			strncat(new_url, uri_string(pathSegment->text, buf, DEFAULT_BUFFER_SIZE),
					DEFAULT_BUFFER_SIZE - 1);
		}
	}

	/* missing components have null,null in their UriTextRangeA
	 * add query parameters if they exist.
	 */
	if (uri.query.first && uri.query.afterLast) {
		// Ensure we have space for '?' + query_str + '\0' ahead of time, instead of calling strncat
		// twice
		size_t current_len = strlen(new_url);
		size_t remaining_space = DEFAULT_BUFFER_SIZE - current_len - 1;

		const char *query_str = uri_string(uri.query, buf, DEFAULT_BUFFER_SIZE);
		size_t query_str_len = strlen(query_str);

		if (remaining_space >= query_str_len + 1) {
			strcat(new_url, "?");
			strcat(new_url, query_str);
		} else {
			die(STATE_UNKNOWN,
				_("HTTP UNKNOWN - No space to add query part of size %zu to the buffer, buffer has "
				  "remaining size %zu"),
				query_str_len, current_len);
		}
	}

	if (working_state.serverPort == new_port &&
		!strncmp(working_state.server_address, new_host, MAX_IPV4_HOSTLENGTH) &&
		(working_state.host_name &&
		 !strncmp(working_state.host_name, new_host, MAX_IPV4_HOSTLENGTH)) &&
		!strcmp(working_state.server_url, new_url)) {
		die(STATE_CRITICAL,
			_("HTTP CRITICAL - redirection creates an infinite loop - %s://%s:%d%s\n"),
			working_state.use_ssl ? "https" : "http", new_host, new_port, new_url);
	}

	/* set new values for redirected request */

	if (!(config.followsticky & STICKY_HOST)) {
		// free(working_state.server_address);
		working_state.server_address = strndup(new_host, MAX_IPV4_HOSTLENGTH);
	}
	if (!(config.followsticky & STICKY_PORT)) {
		working_state.serverPort = (unsigned short)new_port;
	}

	// free(working_state.host_name);
	working_state.host_name = strndup(new_host, MAX_IPV4_HOSTLENGTH);

	/* reset virtual port */
	working_state.virtualPort = working_state.serverPort;

	free(new_host);
	// free(working_state.server_url);
	working_state.server_url = new_url;

	uriFreeUriMembersA(&uri);

	if (verbose) {
		printf(_("Redirection to %s://%s:%d%s\n"), working_state.use_ssl ? "https" : "http",
			   working_state.host_name ? working_state.host_name : working_state.server_address,
			   working_state.serverPort, working_state.server_url);
	}

	/* TODO: the hash component MUST be taken from the original URL and
	 * attached to the URL in Location
	 */

	redir_wrapper result = {
		.redir_depth = redir_depth,
		.working_state = working_state,
		.error_code = OK,
	};
	return result;
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
		STATE_REGEX,
		OUTPUT_FORMAT,
		NO_PROXY,
	};

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
		{"proxy", required_argument, 0, 'x'},
		{"noproxy", required_argument, 0, NO_PROXY},
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
		{"output-format", required_argument, 0, OUTPUT_FORMAT},
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
	int cflags = REG_NOSUB | REG_EXTENDED | REG_NEWLINE;
	bool specify_port = false;
	bool enable_tls = false;
	char *tls_option_optarg = NULL;

	while (true) {
		int option_index = getopt_long(
			argc, argv, "Vvh46t:c:w:A:k:H:P:j:T:I:a:x:b:d:e:p:s:R:r:u:f:C:J:K:DnlLS::m:M:NEB",
			longopts, &option);
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
				result.config.curl_config.socket_timeout = (int)strtol(optarg, NULL, 10);
			}
			break;
		case 'c': /* critical time threshold */
		{
			mp_range_parsed critical_range = mp_parse_range_string(optarg);
			if (critical_range.error != MP_PARSING_SUCCESS) {
				die(STATE_UNKNOWN, "failed to parse critical threshold: %s", optarg);
			}
			result.config.thlds = mp_thresholds_set_crit(result.config.thlds, critical_range.range);
		} break;
		case 'w': /* warning time threshold */
		{
			mp_range_parsed warning_range = mp_parse_range_string(optarg);

			if (warning_range.error != MP_PARSING_SUCCESS) {
				die(STATE_UNKNOWN, "failed to parse warning threshold: %s", optarg);
			}
			result.config.thlds = mp_thresholds_set_warn(result.config.thlds, warning_range.range);
		} break;
		case 'H': /* virtual host */
			result.config.initial_config.host_name = strdup(optarg);
			char *tmp_string;
			size_t host_name_length;
			if (result.config.initial_config.host_name[0] == '[') {
				if ((tmp_string = strstr(result.config.initial_config.host_name, "]:")) !=
					NULL) { /* [IPv6]:port */
					result.config.initial_config.virtualPort = atoi(tmp_string + 2);
					/* cut off the port */
					host_name_length =
						strlen(result.config.initial_config.host_name) - strlen(tmp_string) - 1;
					free(result.config.initial_config.host_name);
					result.config.initial_config.host_name = strndup(optarg, host_name_length);
				}
			} else if ((tmp_string = strchr(result.config.initial_config.host_name, ':')) != NULL &&
					   strchr(++tmp_string, ':') == NULL) { /* IPv4:port or host:port */
				result.config.initial_config.virtualPort = atoi(tmp_string);
				/* cut off the port */
				host_name_length =
					strlen(result.config.initial_config.host_name) - strlen(tmp_string) - 1;
				free(result.config.initial_config.host_name);
				result.config.initial_config.host_name = strndup(optarg, host_name_length);
			}
			break;
		case 'I': /* internet address */
			result.config.initial_config.server_address = strdup(optarg);
			break;
		case 'u': /* URL path */
			result.config.initial_config.server_url = strdup(optarg);
			break;
		case 'p': /* Server port */
			if (!is_intnonneg(optarg)) {
				usage2(_("Invalid port number, expecting a non-negative number"), optarg);
			} else {
				if (strtol(optarg, NULL, 10) > MAX_PORT) {
					usage2(_("Invalid port number, supplied port number is too big"), optarg);
				}
				result.config.initial_config.serverPort = (unsigned short)strtol(optarg, NULL, 10);
				specify_port = true;
			}
			break;
		case 'a': /* authorization info */
			strncpy(result.config.curl_config.user_auth, optarg, MAX_INPUT_BUFFER - 1);
			result.config.curl_config.user_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'x': /* proxy info */
			strncpy(result.config.curl_config.proxy, optarg, DEFAULT_BUFFER_SIZE - 1);
			result.config.curl_config.proxy[DEFAULT_BUFFER_SIZE - 1] = 0;
			break;
		case 'b': /* proxy-authorization info */
			strncpy(result.config.curl_config.proxy_auth, optarg, MAX_INPUT_BUFFER - 1);
			result.config.curl_config.proxy_auth[MAX_INPUT_BUFFER - 1] = 0;
			break;
		case 'P': /* HTTP POST data in URL encoded format; ignored if settings already */
			if (!result.config.initial_config.http_post_data) {
				result.config.initial_config.http_post_data = strdup(optarg);
			}
			if (!result.config.initial_config.http_method) {
				result.config.initial_config.http_method = strdup("POST");
			}
			break;
		case 'j': /* Set HTTP method */
			if (result.config.initial_config.http_method) {
				free(result.config.initial_config.http_method);
			}
			result.config.initial_config.http_method = strdup(optarg);
			break;
		case 'A': /* useragent */
			strncpy(result.config.curl_config.user_agent, optarg, DEFAULT_BUFFER_SIZE);
			result.config.curl_config.user_agent[DEFAULT_BUFFER_SIZE - 1] = '\0';
			break;
		case 'k': /* Additional headers */
			if (result.config.curl_config.http_opt_headers_count == 0) {
				result.config.curl_config.http_opt_headers =
					malloc(sizeof(char *) * (++result.config.curl_config.http_opt_headers_count));
			} else {
				result.config.curl_config.http_opt_headers =
					realloc(result.config.curl_config.http_opt_headers,
							sizeof(char *) * (++result.config.curl_config.http_opt_headers_count));
			}
			result.config.curl_config
				.http_opt_headers[result.config.curl_config.http_opt_headers_count - 1] = optarg;
			break;
		case 'L': /* show html link */
		case 'n': /* do not show html link */
			// HTML link related options are deprecated
			break;
		case 'C': /* Check SSL cert validity */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			{
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
				enable_tls = true;
			}
			break;
		case CONTINUE_AFTER_CHECK_CERT: /* don't stop after the certificate is checked */
#ifdef HAVE_SSL
			result.config.continue_after_check_cert = true;
			break;
#endif
		case 'J': /* use client certificate */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			test_file(optarg);
			result.config.curl_config.client_cert = optarg;
			enable_tls = true;
			break;
		case 'K': /* use client private key */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			test_file(optarg);
			result.config.curl_config.client_privkey = optarg;
			enable_tls = true;
			break;
		case CA_CERT_OPTION: /* use CA chain file */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			test_file(optarg);
			result.config.curl_config.ca_cert = optarg;
			enable_tls = true;
			break;
		case 'D': /* verify peer certificate & host */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			result.config.curl_config.verify_peer_and_host = true;
			enable_tls = true;
			break;
		case 'S': /* use SSL */
			tls_option_optarg = strdup(optarg);
			enable_tls = true;
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif
			break;
		case SNI_OPTION: /* --sni is parsed, but ignored, the default is true with libcurl */
#ifndef LIBCURL_FEATURE_SSL
			usage4(_("Invalid option - SSL is not available"));
#endif /* LIBCURL_FEATURE_SSL */
			break;
		case MAX_REDIRS_OPTION:
			if (!is_intnonneg(optarg)) {
				usage2(_("Invalid max_redirs count"), optarg);
			} else {
				result.config.max_depth = atoi(optarg);
			}
			break;
		case 'f': /* onredirect */
			if (!strcmp(optarg, "ok")) {
				result.config.on_redirect_result_state = STATE_OK;
				result.config.on_redirect_dependent = false;
			} else if (!strcmp(optarg, "warning")) {
				result.config.on_redirect_result_state = STATE_WARNING;
				result.config.on_redirect_dependent = false;
			} else if (!strcmp(optarg, "critical")) {
				result.config.on_redirect_result_state = STATE_CRITICAL;
				result.config.on_redirect_dependent = false;
			} else if (!strcmp(optarg, "unknown")) {
				result.config.on_redirect_result_state = STATE_UNKNOWN;
				result.config.on_redirect_dependent = false;
			} else if (!strcmp(optarg, "follow")) {
				result.config.on_redirect_dependent = true;
			} else if (!strcmp(optarg, "stickyport")) {
				result.config.on_redirect_dependent = true;
				result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_HOST | STICKY_PORT;
			} else if (!strcmp(optarg, "sticky")) {
				result.config.on_redirect_dependent = true;
				result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_HOST;
			} else if (!strcmp(optarg, "follow")) {
				result.config.on_redirect_dependent = true;
				result.config.followmethod = FOLLOW_HTTP_CURL,
				result.config.followsticky = STICKY_NONE;
			} else if (!strcmp(optarg, "curl")) {
				result.config.on_redirect_dependent = true;
				result.config.followmethod = FOLLOW_LIBCURL;
			} else {
				usage2(_("Invalid onredirect option"), optarg);
			}
			if (verbose >= 2) {
				if (result.config.on_redirect_dependent) {
					printf(_("* Following redirects\n"));
				} else {
					printf(_("* Following redirects set to state %s\n"),
						   state_text(result.config.on_redirect_result_state));
				}
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
			strncpy(result.config.server_expect.string, optarg, MAX_INPUT_BUFFER - 1);
			result.config.server_expect.string[MAX_INPUT_BUFFER - 1] = 0;
			result.config.server_expect.is_present = true;
			break;
		case 'T': /* Content-type */
			result.config.curl_config.http_content_type = strdup(optarg);
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

			result.config.compiled_regex = preg;
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
			result.config.curl_config.sin_family = AF_INET;
			break;
		case '6':
#if defined(LIBCURL_FEATURE_IPV6)
			result.config.curl_config.sin_family = AF_INET6;
#else
			usage4(_("IPv6 support not available"));
#endif
			break;
		case 'm': /* min_page_length */
		{
			mp_range_parsed foo = mp_parse_range_string(optarg);

			if (foo.error != MP_PARSING_SUCCESS) {
				die(STATE_CRITICAL, "failed to parse page size limits: %s", optarg);
			}

			result.config.page_length_limits = foo.range;
			result.config.page_length_limits_is_set = true;
			break;
		}
		case 'N': /* no-body */
			result.config.initial_config.no_body = true;
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
			} else if (option_length &&
					   (optarg[option_length - 1] == 's' || isdigit(optarg[option_length - 1]))) {
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
			result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_NONE;
			if (strcmp(optarg, "1.0") == 0) {
				result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_1_0;
			} else if (strcmp(optarg, "1.1") == 0) {
				result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_1_1;
			} else if ((strcmp(optarg, "2.0") == 0) || (strcmp(optarg, "2") == 0)) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 33, 0)
				result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_2_0;
#else
				result.config.curl_http_version = CURL_HTTP_VERSION_NONE;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 33, 0) */
			} else if ((strcmp(optarg, "3") == 0)) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 66, 0)
				result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_3;
#else
				result.config.curl_config.curl_http_version = CURL_HTTP_VERSION_NONE;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 66, 0) */
			} else {
				fprintf(stderr, "unknown http-version parameter: %s\n", optarg);
				exit(STATE_WARNING);
			}
			break;
		case AUTOMATIC_DECOMPRESSION:
			result.config.curl_config.automatic_decompression = true;
			break;
		case COOKIE_JAR:
			result.config.curl_config.cookie_jar_file = optarg;
			break;
		case HAPROXY_PROTOCOL:
			result.config.curl_config.haproxy_protocol = true;
			break;
		case NO_PROXY:
			strncpy(result.config.curl_config.no_proxy, optarg, DEFAULT_BUFFER_SIZE - 1);
			result.config.curl_config.no_proxy[DEFAULT_BUFFER_SIZE - 1] = 0;
			break;
		case '?':
			/* print short usage statement if args not parsable */
			usage5();
			break;
		case OUTPUT_FORMAT: {
			parsed_output_format parser = mp_parse_output_format(optarg);
			if (!parser.parsing_success) {
				// TODO List all available formats here, maybe add anothoer usage function
				printf("Invalid output format: %s\n", optarg);
				exit(STATE_UNKNOWN);
			}

			result.config.output_format_is_set = true;
			result.config.output_format = parser.output_format;
			break;
		}
		}
	}

	if (enable_tls) {
		bool got_plus = false;
		result.config.initial_config.use_ssl = true;
		/* ssl_version initialized to CURL_SSLVERSION_DEFAULT as a default.
		 * Only set if it's non-zero.  This helps when we include multiple
		 * parameters, like -S and -C combinations */
		result.config.curl_config.ssl_version = CURL_SSLVERSION_DEFAULT;
		if (tls_option_optarg != NULL) {
			char *plus_ptr = strchr(tls_option_optarg, '+');
			if (plus_ptr) {
				got_plus = true;
				*plus_ptr = '\0';
			}

			if (tls_option_optarg[0] == '2') {
				result.config.curl_config.ssl_version = CURL_SSLVERSION_SSLv2;
			} else if (tls_option_optarg[0] == '3') {
				result.config.curl_config.ssl_version = CURL_SSLVERSION_SSLv3;
			} else if (!strcmp(tls_option_optarg, "1") || !strcmp(tls_option_optarg, "1.0")) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
				result.config.curl_config.ssl_version = CURL_SSLVERSION_TLSv1_0;
#else
				result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			} else if (!strcmp(tls_option_optarg, "1.1")) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
				result.config.curl_config.ssl_version = CURL_SSLVERSION_TLSv1_1;
#else
				result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			} else if (!strcmp(tls_option_optarg, "1.2")) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0)
				result.config.curl_config.ssl_version = CURL_SSLVERSION_TLSv1_2;
#else
				result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 34, 0) */
			} else if (!strcmp(tls_option_optarg, "1.3")) {
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0)
				result.config.curl_config.ssl_version = CURL_SSLVERSION_TLSv1_3;
#else
				result.config.ssl_version = CURL_SSLVERSION_DEFAULT;
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 52, 0) */
			} else {
				usage4(_("Invalid option - Valid SSL/TLS versions: 2, 3, 1, 1.1, 1.2, 1.3 "
						 "(with optional '+' suffix)"));
			}
		}
#if LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0)
		if (got_plus) {
			switch (result.config.curl_config.ssl_version) {
			case CURL_SSLVERSION_TLSv1_3:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
				break;
			case CURL_SSLVERSION_TLSv1_2:
			case CURL_SSLVERSION_TLSv1_1:
			case CURL_SSLVERSION_TLSv1_0:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_DEFAULT;
				break;
			}
		} else {
			switch (result.config.curl_config.ssl_version) {
			case CURL_SSLVERSION_TLSv1_3:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_3;
				break;
			case CURL_SSLVERSION_TLSv1_2:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_2;
				break;
			case CURL_SSLVERSION_TLSv1_1:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_1;
				break;
			case CURL_SSLVERSION_TLSv1_0:
				result.config.curl_config.ssl_version |= CURL_SSLVERSION_MAX_TLSv1_0;
				break;
			}
		}
#endif /* LIBCURL_VERSION_NUM >= MAKE_LIBCURL_VERSION(7, 54, 0) */
		if (verbose >= 2) {
			printf(_("* Set SSL/TLS version to %ld\n"), result.config.curl_config.ssl_version);
		}
		if (!specify_port) {
			result.config.initial_config.serverPort = HTTPS_PORT;
		}
	}

	int option_counter = optind;

	if (result.config.initial_config.server_address == NULL && option_counter < argc) {
		result.config.initial_config.server_address = strdup(argv[option_counter++]);
	}

	if (result.config.initial_config.host_name == NULL && option_counter < argc) {
		result.config.initial_config.host_name = strdup(argv[option_counter++]);
	}

	if (result.config.initial_config.server_address == NULL) {
		if (result.config.initial_config.host_name == NULL) {
			usage4(_("You must specify a server address or host name"));
		} else {
			result.config.initial_config.server_address =
				strdup(result.config.initial_config.host_name);
		}
	}

	if (result.config.initial_config.http_method == NULL) {
		result.config.initial_config.http_method = strdup("GET");
	}

	if (result.config.curl_config.client_cert && !result.config.curl_config.client_privkey) {
		usage4(_("If you use a client certificate you must also specify a private key file"));
	}

	if (result.config.initial_config.virtualPort == 0) {
		result.config.initial_config.virtualPort = result.config.initial_config.serverPort;
	} else {
		if ((result.config.initial_config.use_ssl &&
			 result.config.initial_config.serverPort == HTTPS_PORT) ||
			(!result.config.initial_config.use_ssl &&
			 result.config.initial_config.serverPort == HTTP_PORT)) {
			if (!specify_port) {
				result.config.initial_config.serverPort = result.config.initial_config.virtualPort;
			}
		}
	}

	return result;
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
	printf("%s\n",
		   _("It makes use of libcurl to do so. It tries to be as compatible to check_http"));
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
	printf("    %s\n",
		   "IP address or name (use numeric address if possible to bypass DNS lookup).");
	printf("    %s\n",
		     "This overwrites the network address of the target while leaving everything else (HTTP headers) as they are");
	printf(" %s\n", "-p, --port=INTEGER");
	printf("    %s", _("Port number (default: "));
	printf("%d)\n", HTTP_PORT);

	printf(UT_IPv46);

#ifdef LIBCURL_FEATURE_SSL
	printf(" %s\n", "-S, --ssl=VERSION[+]");
	printf("    %s\n",
		   _("Connect via SSL. Port defaults to 443. VERSION is optional, and prevents"));
	printf("    %s\n", _("auto-negotiation (2 = SSLv2, 3 = SSLv3, 1 = TLSv1, 1.1 = TLSv1.1,"));
	printf("    %s\n", _("1.2 = TLSv1.2, 1.3 = TLSv1.3). With a '+' suffix, newer versions are "
						 "also accepted."));
	printf("    %s\n", _("Note: SSLv2, SSLv3, TLSv1.0 and TLSv1.1 are deprecated and are usually "
						 "disabled in libcurl"));
	printf(" %s\n", "--sni");
	printf("    %s\n", _("Enable SSL/TLS hostname extension support (SNI)"));
#	if LIBCURL_VERSION_NUM >= 0x071801
	printf("    %s\n",
		   _("Note: --sni is the default in libcurl as SSLv2 and SSLV3 are deprecated and"));
	printf("    %s\n", _("      SNI only really works since TLSv1.0"));
#	else
	printf("    %s\n", _("Note: SNI is not supported in libcurl before 7.18.1"));
#	endif
	printf(" %s\n", "-C, --certificate=INTEGER[,INTEGER]");
	printf("    %s\n",
		   _("Minimum number of days a certificate has to be valid. Port defaults to 443."));
	printf("    %s\n",
		   _("A STATE_WARNING is returned if the certificate has a validity less than the"));
	printf("    %s\n",
		   _("first agument's value. If there is a second argument and the certificate's"));
	printf("    %s\n", _("validity is less than its value, a STATE_CRITICAL is returned."));
	printf("    %s\n",
		   _("(When this option is used the URL is not checked by default. You can use"));
	printf("    %s\n", _(" --continue-after-certificate to override this behavior)"));
	printf(" %s\n", "--continue-after-certificate");
	printf("    %s\n",
		   _("Allows the HTTP check to continue after performing the certificate check."));
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
	printf("    %s\n",
		   _("If specified skips all other status line logic (ex: 3xx, 4xx, 5xx processing)"));
	printf(" %s\n", "-d, --header-string=STRING");
	printf("    %s\n", _("String to expect in the response headers"));
	printf(" %s\n", "-s, --string=STRING");
	printf("    %s\n", _("String to expect in the content"));
	printf(" %s\n", "-u, --url=PATH");
	printf("    %s\n", _("URL to GET or POST (default: /)"));
	printf("    %s\n", _("This is the part after the address in a URL, so for \"https://example.com/index.html\" it would be '-u /index.html'"));
	printf(" %s\n", "-P, --post=STRING");
	printf("    %s\n", _("URL decoded http POST data"));
	printf(" %s\n",
		   "-j, --method=STRING  (for example: HEAD, OPTIONS, TRACE, PUT, DELETE, CONNECT)");
	printf("    %s\n", _("Set HTTP method."));
	printf(" %s\n", "-N, --no-body");
	printf("    %s\n", _("Don't wait for document body: stop reading after headers."));
	printf("    %s\n", _("(Note that this still does an HTTP GET or POST, not a HEAD.)"));
	printf(" %s\n", "-M, --max-age=SECONDS");
	printf("    %s\n", _("Warn if document is more than SECONDS old. the number can also be of"));
	printf("    %s\n", _("the form \"10m\" for minutes, \"10h\" for hours, or \"10d\" for days."));
	printf(" %s\n", "-T, --content-type=STRING");
	printf("    %s\n", _("specify Content-Type header media type when POSTing"));
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
	printf("    %s\n", _("Return STATE if regex is found, OK if not. STATE can be one of "
						 "\"critical\",\"warning\""));
	printf(" %s\n", "-x, --proxy=PROXY_SERVER");
	printf("    %s\n", _("Specify the proxy in form of <scheme>://<host(name)>:<port>"));
	printf("    %s\n", _("Available schemes are http, https, socks4, socks4a, socks5, socks5h"));
	printf("    %s\n", _("If port is not specified, libcurl defaults to 1080"));
	printf("    %s\n", _("This value will be set as CURLOPT_PROXY"));
	printf(" %s\n", "--noproxy=COMMA_SEPARATED_LIST");
	printf("    %s\n", _("Specify hostnames, addresses and subnets where proxy should not be used"));
	printf("    %s\n", _("Example usage: \"example.com,::1,1.1.1.1,localhost,192.168.0.0/16\""));
	printf("    %s\n", _("Do not use brackets when specifying IPv6 addresses"));
	printf("    %s\n", _("Special case when an item is '*' : matches all hosts/addresses "
		"and effectively disables proxy."));
	printf("    %s\n", _("This value will be set as CURLOPT_NOPROXY"));
	printf(" %s\n", "-a, --authorization=AUTH_PAIR");
	printf("    %s\n", _("Username:password on sites with basic authentication"));
	printf(" %s\n", "-b, --proxy-authorization=AUTH_PAIR");
	printf("    %s\n", _("Username:password on proxy-servers with basic authentication"));
	printf(" %s\n", "-A, --useragent=STRING");
	printf("    %s\n", _("String to be sent in http header as \"User Agent\""));
	printf(" %s\n", "-k, --header=STRING");
	printf("    %s\n", _("Any other tags to be sent in http header. Use multiple times for "
						 "additional headers"));
	printf(" %s\n", "-E, --extended-perfdata");
	printf("    %s\n", _("Print additional performance data"));
	printf(" %s\n", "-B, --show-body");
	printf("    %s\n", _("Print body content below status line"));
	// printf(" %s\n", "-L, --link");
	// printf("    %s\n", _("Wrap output in HTML link (obsoleted by urlize)"));
	printf(" %s\n", "-f, --onredirect=<ok|warning|critical|follow|sticky|stickyport|curl>");
	printf("    %s\n", _("How to handle redirected pages. sticky is like follow but stick to the"));
	printf("    %s\n", _("specified IP address. stickyport also ensures port stays the same."));
	printf("    %s\n", _("follow uses the old redirection algorithm of check_http."));
	printf("    %s\n", _("curl uses CURL_FOLLOWLOCATION built into libcurl."));
	printf(" %s\n", "--max-redirs=INTEGER");
	printf("    %s", _("Maximal number of redirects (default: "));
	printf("%d)\n", DEFAULT_MAX_REDIRS);
	printf(" %s\n", "-m, --pagesize=INTEGER<:INTEGER>");
	printf("    %s\n",
		   _("Minimum page size required (bytes) : Maximum page size required (bytes)"));
	printf("\n");
	printf(" %s\n", "--http-version=VERSION");
	printf("    %s\n", _("Connect via specific HTTP protocol."));
	printf("    %s\n",
		   _("1.0 = HTTP/1.0, 1.1 = HTTP/1.1, 2.0 = HTTP/2 (HTTP/2 will fail without -S)"));
	printf(" %s\n", "--enable-automatic-decompression");
	printf("    %s\n", _("Enable automatic decompression of body (CURLOPT_ACCEPT_ENCODING)."));
	printf(" %s\n", "--haproxy-protocol");
	printf("    %s\n", _("Send HAProxy proxy protocol v1 header (CURLOPT_HAPROXYPROTOCOL)."));
	printf(" %s\n", "--cookie-jar=FILE");
	printf("    %s\n", _("Store cookies in the cookie jar and send them out when requested."));
	printf("    %s\n",
		   _("Specify an empty string as FILE to enable curl's cookie engine without saving"));
	printf("    %s\n",
		   _("the cookies to disk. Only enabling the engine without saving to disk requires"));
	printf("    %s\n",
		   _("handling multiple requests internally to curl, so use it with --onredirect=curl"));
	printf("\n");

	printf(UT_WARN_CRIT);

	printf(UT_CONN_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf(UT_VERBOSE);

	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("This plugin will attempt to open an HTTP connection with the host."));
	printf(" %s\n",
		   _("Successful connects return STATE_OK, refusals and timeouts return STATE_CRITICAL"));
	printf(" %s\n",
		   _("other errors return STATE_UNKNOWN.  Successful connects, but incorrect response"));
	printf(" %s\n", _("messages from the host result in STATE_WARNING return values.  If you are"));
	printf(" %s\n",
		   _("checking a virtual server that uses 'host headers' you must supply the FQDN"));
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
	printf(" %s\n", _("To also verify certificates, please set --verify-cert."));
	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n\n", "CHECK CONTENT: check_curl -w 5 -c 10 --ssl -H www.verisign.com");
	printf(" %s\n", _("When the 'www.verisign.com' server returns its content within 5 seconds,"));
	printf(" %s\n",
		   _("a STATE_OK will be returned. When the server returns its content but exceeds"));
	printf(" %s\n",
		   _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
	printf(" %s\n", _("a STATE_CRITICAL will be returned."));
	printf("\n");
	printf(" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 14 -D");
	printf(" %s\n",
		   _("When the certificate of 'www.verisign.com' is valid for more than 14 days,"));
	printf(" %s\n",
		   _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
	printf(" %s\n",
		   _("14 days, a STATE_WARNING is returned. A STATE_CRITICAL will be returned when"));
	printf(" %s\n", _("the certificate is expired."));
	printf("\n");
	printf(" %s\n", _("The -D flag enforces a certificate validation beyond expiration time."));
	printf("\n");
	printf(" %s\n\n", "CHECK CERTIFICATE: check_curl -H www.verisign.com -C 30,14 -D");
	printf(" %s\n",
		   _("When the certificate of 'www.verisign.com' is valid for more than 30 days,"));
	printf(" %s\n",
		   _("a STATE_OK is returned. When the certificate is still valid, but for less than"));
	printf(" %s\n", _("30 days, but more than 14 days, a STATE_WARNING is returned."));
	printf(" %s\n",
		   _("A STATE_CRITICAL will be returned when certificate expires in less than 14 days"));
#endif

	printf("\n %s\n", "CHECK WEBSERVER CONTENT VIA PROXY:");
	printf(" %s\n", _("Proxies are specified or disabled for certain hosts/addresses using environment variables"
		" or -x/--proxy and --noproxy arguments:"));
	printf(" %s\n", _("Checked environment variables: all_proxy, http_proxy, https_proxy, no_proxy"));
	printf(" %s\n", _("Environment variables can also be given in uppercase, but the lowercase ones will "
					  "take predence if both are defined."));
	printf(" %s\n", _("The environment variables are overwritten by -x/--proxy and --noproxy arguments:"));
	printf(" %s\n", _("all_proxy/ALL_PROXY environment variables are read first, but protocol "
					"specific environment variables override them."));
	printf(" %s\n", _("If SSL is enabled and used, https_proxy/HTTPS_PROXY will be checked and overwrite "
			"http_proxy/HTTPS_PROXY."));
	printf(" %s\n", _("Curl accepts proxies using http, https, socks4, socks4a, socks5 and socks5h schemes."));
	printf(" %s\n", _("http_proxy=http://192.168.100.35:3128 ./check_curl -H www.monitoring-plugins.org"));
	printf(" %s\n", _("http_proxy=http://used.proxy.com HTTP_PROXY=http://ignored.proxy.com ./check_curl -H www.monitoring-plugins.org"));
	printf(" %s\n", _("  Lowercase http_proxy takes predence over uppercase HTTP_PROXY"));
	printf(" %s\n", _("./check_curl -H www.monitoring-plugins.org -x http://192.168.100.35:3128"));
	printf(" %s\n", _("http_proxy=http://unused.proxy1.com HTTP_PROXY=http://unused.proxy2.com ./check_curl "
			"-H www.monitoring-plugins.org --proxy http://used.proxy"));
	printf(" %s\n", _("  Proxy specified by --proxy overrides any proxy specified by environment variable."));
	printf(" %s\n", _("  Curl uses port 1080 by default as port is not specified"));
	printf(" %s\n", _("HTTPS_PROXY=http://192.168.100.35:3128 ./check_curl -H www.monitoring-plugins.org --ssl"));
	printf(" %s\n", _("  HTTPS_PROXY is read as --ssl is toggled"));
	printf(" %s\n", _("./check_curl -H www.monitoring-plugins.org --proxy socks5h://192.168.122.21"));
	printf(" %s\n", _("./check_curl -H www.monitoring-plugins.org -x http://unused.proxy.com --noproxy '*'"));
	printf(" %s\n", _("  Disabled proxy for all hosts by using '*' in no_proxy ."));
	printf(" %s\n", _("NO_PROXY=www.monitoring-plugins.org ./check_curl -H www.monitoring-plugins.org -x http://unused.proxy.com"));
	printf(" %s\n", _("  Exact matches with the hostname/address work."));
	printf(" %s\n", _("no_proxy=192.168.178.0/24 ./check_curl -I 192.168.178.10 -x http://proxy.acme.org"));
	printf(" %s\n", _("no_proxy=acme.org ./check_curl -H nonpublic.internalwebapp.acme.org -x http://proxy.acme.org"));
	printf(" %s\n", _("  Do not use proxy when accessing internal domains/addresses, but use a default proxy when accessing public web."));
	printf(" %s\n", _("  IMPORTANT: Check_curl can not always determine whether itself or the proxy will "
		"resolve a hostname before sending a request and getting an answer."
		"This can lead to DNS resolvation issues if hostname is only resolvable over proxy."));
	printf(" %s\n", _("Legacy proxy requests in check_http style still work:"));
	printf(" %s\n", _("check_curl -I 192.168.100.35 -p 3128 -u http://www.monitoring-plugins.org/ "
					  "-H www.monitoring-plugins.org"));

#ifdef LIBCURL_FEATURE_SSL
	printf("\n %s\n", "CHECK SSL WEBSERVER CONTENT VIA PROXY USING HTTP 1.1 CONNECT: ");
	printf(" %s\n", _("It is recommended to use an environment proxy like:"));
	printf(" %s\n",
		   _("https_proxy=http://192.168.100.35:3128 ./check_curl -H www.verisign.com -S"));
	printf(" %s\n", _("legacy proxy requests in check_http style might still work, but are frowned "
					  "upon, so DONT:"));
	printf(" %s\n", _("check_curl -I 192.168.100.35 -p 3128 -u https://www.verisign.com/ -S -j "
					  "CONNECT -H www.verisign.com "));
	printf(" %s\n", _("all these options are needed: -I <proxy> -p <proxy-port> -u <check-url> "
					  "-S(sl) -j CONNECT -H <webserver>"));
	printf(" %s\n",
		   _("a STATE_OK will be returned. When the server returns its content but exceeds"));
	printf(" %s\n",
		   _("the 5-second threshold, a STATE_WARNING will be returned. When an error occurs,"));
	printf(" %s\n", _("a STATE_CRITICAL will be returned."));

#endif

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s -H <vhost> | -I <IP-address> [-u <uri>] [-p <port>]\n", progname);
	printf("       [-J <client certificate file>] [-K <private key>] [--ca-cert <CA certificate "
		   "file>] [-D]\n");
	printf("       [-w <warn time>] [-c <critical time>] [-t <timeout>] [-L] [-E] [-x <proxy>]\n");
	printf("       [-a auth] [-b proxy_auth] [-f "
		   "<ok|warning|critical|follow|sticky|stickyport|curl>]\n");
	printf("       [-e <expect>] [-d string] [-s string] [-l] [-r <regex> | -R <case-insensitive "
		   "regex>]\n");
	printf("       [-P string] [-m <min_pg_size>:<max_pg_size>] [-4|-6] [-N] [-M <age>]\n");
	printf("       [-A string] [-k string] [-S <version>] [--sni] [--haproxy-protocol]\n");
	printf("       [-T <content-type>] [-j method]\n");
	printf("       [--noproxy=<comma separated list of hosts, IP addresses, IP CIDR subnets>\n");
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
#	endif /* USE_OPENSSL */
#endif     /* LIBCURL_FEATURE_SSL */

#ifdef LIBCURL_FEATURE_SSL
#	ifndef USE_OPENSSL
/* TODO: this needs cleanup in the sslutils.c, maybe we the #else case to
 * OpenSSL could be this function
 */
int net_noopenssl_check_certificate(cert_ptr_union *cert_ptr, int days_till_exp_warn,
									int days_till_exp_crit) {

	if (verbose >= 2) {
		printf("**** REQUEST CERTIFICATES ****\n");
	}

	char *start_date_str = NULL;
	char *end_date_str = NULL;
	bool have_first_cert = false;
	bool cname_found = false;
	for (int i = 0; i < cert_ptr->to_certinfo->num_of_certs; i++) {
		if (have_first_cert) {
			break;
		}

		struct curl_slist *slist;
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
						cname_found = true;
					}
				}
			} else if (strncasecmp(slist->data, "Start Date:", 11) == 0) {
				start_date_str = &slist->data[11];
			} else if (strncasecmp(slist->data, "Expire Date:", 12) == 0) {
				end_date_str = &slist->data[12];
			} else if (strncasecmp(slist->data, "Cert:", 5) == 0) {
				have_first_cert = true;
				break;
			}
			if (verbose >= 2) {
				printf("%d ** %s\n", i, slist->data);
			}
		}
	}

	if (verbose >= 2) {
		printf("**** REQUEST CERTIFICATES ****\n");
	}

	if (!cname_found) {
		printf("%s\n", _("CRITICAL - Cannot retrieve certificate subject."));
		return STATE_CRITICAL;
	}

	time_t start_date = parse_cert_date(start_date_str);
	if (start_date <= 0) {
		snprintf(msg, DEFAULT_BUFFER_SIZE,
				 _("WARNING - Unparsable 'Start Date' in certificate: '%s'"), start_date_str);
		puts(msg);
		return STATE_WARNING;
	}

	time_t end_date = parse_cert_date(end_date_str);
	if (end_date <= 0) {
		snprintf(msg, DEFAULT_BUFFER_SIZE,
				 _("WARNING - Unparsable 'Expire Date' in certificate: '%s'"), start_date_str);
		puts(msg);
		return STATE_WARNING;
	}

	float time_left = difftime(end_date, time(NULL));
	int days_left = time_left / 86400;
	char *tz = getenv("TZ");
	setenv("TZ", "GMT", 1);
	tzset();

	char timestamp[50] = "";
	strftime(timestamp, 50, "%c %z", localtime(&end_date));
	if (tz) {
		setenv("TZ", tz, 1);
	} else {
		unsetenv("TZ");
	}
	tzset();

	mp_state_enum status = STATE_UNKNOWN;
	int time_remaining;
	if (days_left > 0 && days_left <= days_till_exp_warn) {
		printf(_("%s - Certificate '%s' expires in %d day(s) (%s).\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", host_name, days_left,
			   timestamp);
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

		printf(_("%s - Certificate '%s' expires in %u %s (%s)\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", host_name, time_remaining,
			   time_left >= 3600 ? "hours" : "minutes", timestamp);

		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else if (time_left < 0) {
		printf(_("CRITICAL - Certificate '%s' expired on %s.\n"), host_name, timestamp);
		status = STATE_CRITICAL;
	} else if (days_left == 0) {
		printf(_("%s - Certificate '%s' just expired (%s).\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", host_name, timestamp);
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
