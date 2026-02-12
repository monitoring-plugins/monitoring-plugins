#include "./config.h"
#include <curl/curl.h>
#include "../picohttpparser/picohttpparser.h"
#include "output.h"

#if defined(HAVE_SSL) && defined(USE_OPENSSL)
#	include <openssl/opensslv.h>
#endif

enum {
	MAX_IPV4_HOSTLENGTH = 255,
};

/* for buffers for header and body */
typedef struct {
	size_t buflen;
	size_t bufsize;
	char *buf;
} curlhelp_write_curlbuf;

/* for buffering the data sent in PUT */
typedef struct {
	size_t buflen;
	off_t pos;
	char *buf;
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

typedef struct {
	bool curl_global_initialized;
	bool curl_easy_initialized;

	bool body_buf_initialized;
	curlhelp_write_curlbuf *body_buf;

	bool header_buf_initialized;
	curlhelp_write_curlbuf *header_buf;

	bool status_line_initialized;
	curlhelp_statusline *status_line;

	bool put_buf_initialized;
	curlhelp_read_curlbuf *put_buf;

	CURL *curl;

	struct curl_slist *header_list;
	struct curl_slist *host;
} check_curl_global_state;

/* to know the underlying SSL library used by libcurl */
typedef enum curlhelp_ssl_library {
	CURLHELP_SSL_LIBRARY_UNKNOWN,
	CURLHELP_SSL_LIBRARY_OPENSSL,
	CURLHELP_SSL_LIBRARY_LIBRESSL,
	CURLHELP_SSL_LIBRARY_GNUTLS,
	CURLHELP_SSL_LIBRARY_NSS
} curlhelp_ssl_library;

#define MAKE_LIBCURL_VERSION(major, minor, patch) ((major) * 0x10000 + (minor) * 0x100 + (patch))

typedef struct {
	int errorcode;
	check_curl_global_state curl_state;
	check_curl_working_state working_state;
} check_curl_configure_curl_wrapper;

check_curl_configure_curl_wrapper check_curl_configure_curl(check_curl_static_curl_config config,
															check_curl_working_state working_state,
															bool check_cert,
															bool on_redirect_dependent,
															int follow_method, long max_depth);

void handle_curl_option_return_code(CURLcode res, const char *option);

int curlhelp_initwritebuffer(curlhelp_write_curlbuf **buf);
size_t curlhelp_buffer_write_callback(void * /*buffer*/, size_t /*size*/, size_t /*nmemb*/,
									  void * /*stream*/);
void curlhelp_freewritebuffer(curlhelp_write_curlbuf * /*buf*/);

int curlhelp_initreadbuffer(curlhelp_read_curlbuf **buf, const char * /*data*/, size_t /*datalen*/);
size_t curlhelp_buffer_read_callback(void * /*buffer*/, size_t /*size*/, size_t /*nmemb*/,
									 void * /*stream*/);
void curlhelp_freereadbuffer(curlhelp_read_curlbuf * /*buf*/);

curlhelp_ssl_library curlhelp_get_ssl_library(void);
const char *curlhelp_get_ssl_library_string(curlhelp_ssl_library /*ssl_library*/);

typedef union {
	struct curl_slist *to_info;
	struct curl_certinfo *to_certinfo;
} cert_ptr_union;
int net_noopenssl_check_certificate(cert_ptr_union *, int, int);

int curlhelp_parse_statusline(const char * /*buf*/, curlhelp_statusline * /*status_line*/);
void curlhelp_free_statusline(curlhelp_statusline * /*status_line*/);

char *get_header_value(const struct phr_header *headers, size_t nof_headers, const char *header);
mp_subcheck check_document_dates(const curlhelp_write_curlbuf * /*header_buf*/,
								 int /*maximum_age*/);
size_t get_content_length(const curlhelp_write_curlbuf *header_buf,
						  const curlhelp_write_curlbuf *body_buf);
int lookup_host(const char *host, char *buf, size_t buflen, sa_family_t addr_family);
CURLcode sslctxfun(CURL *curl, SSL_CTX *sslctx, void *parm);

#define INET_ADDR_MAX_SIZE INET6_ADDRSTRLEN
const char *strrstr2(const char *haystack, const char *needle);

void cleanup(check_curl_global_state global_state);

bool expected_statuscode(const char *reply, const char *statuscodes);
char *string_statuscode(int major, int minor);

void test_file(char *path);
mp_subcheck check_curl_certificate_checks(CURL *curl, X509 *cert, int warn_days_till_exp,
										  int crit_days_till_exp);
char *fmt_url(check_curl_working_state workingState);

/* returns 0 if requester resolves the hostname locally, 1 if proxy resolves the hostname*/
int determine_hostname_resolver(const check_curl_working_state working_state, const check_curl_static_curl_config config);
