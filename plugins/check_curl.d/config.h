#pragma once

#include "../../config.h"
#include "common.h"
#include "states.h"
#include "thresholds.h"
#include <stddef.h>
#include <string.h>
#include "curl/curl.h"

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
	unsigned short server_port;
	unsigned short virtual_port;
	char *host_name;
	char *server_url;

	bool automatic_decompression;
	bool haproxy_protocol;
	char *client_cert;
	char *client_privkey;
	char *ca_cert;
	bool use_ssl;
	int ssl_version;
	char *http_method;
	char user_agent[DEFAULT_BUFFER_SIZE];
	char **http_opt_headers;
	int http_opt_headers_count;
	char *http_post_data;
	int max_depth;
	char *http_content_type;
	long socket_timeout;
	char user_auth[MAX_INPUT_BUFFER];
	char proxy_auth[MAX_INPUT_BUFFER];
	int followmethod;
	int followsticky;
	bool no_body;
	int curl_http_version;
	char *cookie_jar_file;

	int maximum_age;
	char regexp[MAX_RE_SIZE];
	int state_regex;
	bool invert_regex;
	bool verify_peer_and_host;
	bool check_cert;
	bool continue_after_check_cert;
	int days_till_exp_warn;
	int days_till_exp_crit;
	thresholds *thlds;
	int min_page_len;
	int max_page_len;
	char server_expect[MAX_INPUT_BUFFER];
	bool server_expect_yn;
	char string_expect[MAX_INPUT_BUFFER];
	char header_expect[MAX_INPUT_BUFFER];
	int onredirect;

	bool show_extended_perfdata;
	bool show_body;
	bool display_html;
} check_curl_config;

check_curl_config check_curl_config_init() {
	check_curl_config tmp = {
		.server_address = NULL,
		.server_port = HTTP_PORT,
		.virtual_port = 0,
		.host_name = NULL,
		.server_url = strdup(DEFAULT_SERVER_URL),

		.automatic_decompression = false,
		.haproxy_protocol = false,
		.client_cert = NULL,
		.client_privkey = NULL,
		.ca_cert = NULL,
		.use_ssl = false,
		.ssl_version = CURL_SSLVERSION_DEFAULT,
		.http_method = NULL,
		.user_agent = {},
		.http_opt_headers = NULL,
		.http_opt_headers_count = 0,
		.http_post_data = NULL,
		.max_depth = DEFAULT_MAX_REDIRS,
		.http_content_type = NULL,
		.socket_timeout = DEFAULT_SOCKET_TIMEOUT,
		.user_auth = "",
		.proxy_auth = "",
		.followmethod = FOLLOW_HTTP_CURL,
		.followsticky = STICKY_NONE,
		.no_body = false,
		.curl_http_version = CURL_HTTP_VERSION_NONE,
		.cookie_jar_file = NULL,

		.maximum_age = -1,
		.regexp = {},
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
