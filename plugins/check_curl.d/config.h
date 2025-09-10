#pragma once

#include "../../config.h"
#include "../common.h"
#include "../../lib/states.h"
#include "../../lib/thresholds.h"
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include "curl/curl.h"
#include "regex.h"

enum {
	MAX_RE_SIZE = 1024,
	HTTP_PORT = 80,
	HTTPS_PORT = 443,
	MAX_PORT = 65535,
	DEFAULT_MAX_REDIRS = 15
};

enum {
	FOLLOW_HTTP_CURL = 0,
	FOLLOW_LIBCURL = 1
};

enum {
	STICKY_NONE = 0,
	STICKY_HOST = 1,
	STICKY_PORT = 2
};

#define HTTP_EXPECT         "HTTP/"
#define DEFAULT_BUFFER_SIZE 2048
#define DEFAULT_SERVER_URL  "/"

typedef struct {
	char *server_address;
	char *server_url;
	char *host_name;

	char *http_method;

	char *http_post_data;

	unsigned short virtualPort;
	unsigned short serverPort;

	bool use_ssl;
	bool no_body;
} check_curl_working_state;

check_curl_working_state check_curl_working_state_init() {
	check_curl_working_state result = {
		.server_address = NULL,
		.server_url = DEFAULT_SERVER_URL,
		.host_name = NULL,
		.http_method = NULL,
		.http_post_data = NULL,
		.virtualPort = 0,
		.serverPort = 0,
		.use_ssl = false,
		.no_body = false,
	};
	return result;
}

typedef struct {
	check_curl_working_state initial_config;
	sa_family_t sin_family;

	bool automatic_decompression;
	bool haproxy_protocol;
	char *client_cert;
	char *client_privkey;
	char *ca_cert;
	int ssl_version;
	char user_agent[DEFAULT_BUFFER_SIZE];
	char **http_opt_headers;
	size_t http_opt_headers_count;
	int max_depth;
	char *http_content_type;
	long socket_timeout;
	char user_auth[MAX_INPUT_BUFFER];
	char proxy_auth[MAX_INPUT_BUFFER];
	int followmethod;
	int followsticky;
	int curl_http_version;
	char *cookie_jar_file;

	int maximum_age;

	// the original regex string from the command line
	char regexp[MAX_RE_SIZE];

	// the compiled regex for usage later
	regex_t compiled_regex;

	mp_state_enum state_regex;
	bool invert_regex;
	bool verify_peer_and_host;
	bool check_cert;
	bool continue_after_check_cert;
	int days_till_exp_warn;
	int days_till_exp_crit;
	thresholds *thlds;
	size_t min_page_len;
	size_t max_page_len;
	char server_expect[MAX_INPUT_BUFFER];
	bool server_expect_yn;
	char string_expect[MAX_INPUT_BUFFER];
	char header_expect[MAX_INPUT_BUFFER];
	mp_state_enum onredirect;

	bool show_extended_perfdata;
	bool show_body;
	bool display_html;
} check_curl_config;

check_curl_config check_curl_config_init() {
	check_curl_config tmp = {
		.initial_config = check_curl_working_state_init(),

		.sin_family = AF_UNSPEC,

		.automatic_decompression = false,
		.haproxy_protocol = false,
		.client_cert = NULL,
		.client_privkey = NULL,
		.ca_cert = NULL,
		.ssl_version = CURL_SSLVERSION_DEFAULT,
		.user_agent = {'\0'},
		.http_opt_headers = NULL,
		.http_opt_headers_count = 0,
		.max_depth = DEFAULT_MAX_REDIRS,
		.http_content_type = NULL,
		.socket_timeout = DEFAULT_SOCKET_TIMEOUT,
		.user_auth = "",
		.proxy_auth = "",
		.followmethod = FOLLOW_HTTP_CURL,
		.followsticky = STICKY_NONE,
		.curl_http_version = CURL_HTTP_VERSION_NONE,
		.cookie_jar_file = NULL,

		.maximum_age = -1,
		.regexp = {},
		.compiled_regex = {},
		.state_regex = STATE_CRITICAL,
		.invert_regex = false,
		.verify_peer_and_host = false,
		.check_cert = false,
		.continue_after_check_cert = false,
		.days_till_exp_warn = 0,
		.days_till_exp_crit = 0,
		.thlds = NULL,
		.min_page_len = 0,
		.max_page_len = 0,
		.server_expect = HTTP_EXPECT,
		.server_expect_yn = false,
		.string_expect = "",
		.header_expect = "",
		.onredirect = STATE_OK,

		.show_extended_perfdata = false,
		.show_body = false,
		.display_html = false,
	};
	return tmp;
}
