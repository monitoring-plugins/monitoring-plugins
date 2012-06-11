/*****************************************************************************
* 
* Nagios plugins SSL utilities
* 
* License: GPL
* Copyright (c) 2005-2010 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains common functions for plugins that require SSL.
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

#define MAX_CN_LENGTH 256
#define LOCAL_TIMEOUT_ALARM_HANDLER
#include "common.h"
#include "netutils.h"

#ifdef HAVE_SSL
static SSL_CTX *c=NULL;
static SSL *s=NULL;
static int initialized=0;

/*
 * If a helper can do more than one subkeck (np_net_ssl_check_cert), keep track
 * of the previous checks results so we can output a final status code and
 * message.
 *
 * You *should not* modify directly its fields directly. Instead use
 * ssl_status_* functions to set the struct fields and record new statuses.
 */
#define SSL_STATUS_INIT { \
	.state = 3, /* unknown */ \
	.message = NULL \
}

struct ssl_status {
	int state;
	char *message;
};

static void ssl_status_set (struct ssl_status *s, int state, char *message)
{
	if (!s)
		return;

	s->state = state;
	if (s->message) {
		free(s->message);
		s->message = NULL;
	}

	s->message = strdup(message);
}

/*
 * Frees the allocated message (if any)
 */
static void ssl_status_free (struct ssl_status *s)
{
	if (!s)
		return;

	if (s->message)
		free(s->message);
}

/*
 * Sets new information if criticity is higher than the current status
 */
static void ssl_status_record (struct ssl_status *cur, int state, char *message)
{
	if (!cur || !message)
		return;

	if (max_state(cur->state, state) == cur->state)
		return;

	ssl_status_set(cur, state, message);

}


int np_net_ssl_init (int sd) {
		return np_net_ssl_init_with_hostname(sd, NULL);
}

int np_net_ssl_init_with_hostname(int sd, char *host_name) {
	return np_net_ssl_init_with_hostname_and_version(sd, host_name, 0);
}

int np_net_ssl_init_with_hostname_and_version(int sd, char *host_name, int version) {
	const SSL_METHOD *method = NULL;

	switch (version) {
	case 0: /* Deafult to auto negotiation */
		method = SSLv23_client_method();
		break;
	case 1: /* TLSv1 protocol */
		method = TLSv1_client_method();
		break;
	case 2: /* SSLv2 protocol */
#if defined(USE_GNUTLS) || defined(OPENSSL_NO_SSL2)
		printf(("%s\n", _("CRITICAL - SSL protocol version 2 is not supported by your SSL library.")));
		return STATE_CRITICAL;
#else
		method = SSLv2_client_method();
#endif
		break;
	case 3: /* SSLv3 protocol */
		method = SSLv3_client_method();
		break;
	default: /* Unsupported */
		printf("%s\n", _("CRITICAL - Unsupported SSL protocol version."));
		return STATE_CRITICAL;
	}
	if (!initialized) {
		/* Initialize SSL context */
		SSLeay_add_ssl_algorithms();
		SSL_load_error_strings();
		OpenSSL_add_all_algorithms();
		initialized = 1;
	}
	if ((c = SSL_CTX_new(method)) == NULL) {
		printf("%s\n", _("CRITICAL - Cannot create SSL context."));
		return STATE_CRITICAL;
	}
#ifdef SSL_OP_NO_TICKET
	SSL_CTX_set_options(c, SSL_OP_NO_TICKET);
#endif
	if ((s = SSL_new(c)) != NULL) {
#ifdef SSL_set_tlsext_host_name
		if (host_name != NULL)
			SSL_set_tlsext_host_name(s, host_name);
#endif
		SSL_set_fd(s, sd);
		if (SSL_connect(s) == 1) {
			return OK;
		} else {
			printf("%s\n", _("CRITICAL - Cannot make SSL connection."));
#  ifdef USE_OPENSSL /* XXX look into ERR_error_string */
			ERR_print_errors_fp(stdout);
#  endif /* USE_OPENSSL */
		}
	} else {
			printf("%s\n", _("CRITICAL - Cannot initiate SSL handshake."));
	}
	return STATE_CRITICAL;
}

void np_net_ssl_cleanup() {
	if (s) {
#ifdef SSL_set_tlsext_host_name
		SSL_set_tlsext_host_name(s, NULL);
#endif
		SSL_shutdown(s);
		SSL_free(s);
		if (c) {
			SSL_CTX_free(c);
			c=NULL;
		}
		s=NULL;
	}
}

int np_net_ssl_write(const void *buf, int num) {
	return SSL_write(s, buf, num);
}

int np_net_ssl_read(void *buf, int num) {
	return SSL_read(s, buf, num);
}

static unsigned hex2val(unsigned const char c)
{
	if (isdigit(c))
		return c - '0';

	if ((c >= 'a') && (c <= 'f'))
		return 10 + (c - 'a');

	if ((c >= 'A') && (c <= 'F'))
		return 10 + (c - 'A');

	return ~0;

}

static int hex2sha1(unsigned char *sha1, unsigned const char *hex)
{
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = (hex2val(*hex) << 4) | hex2val(*++hex);
		if (val & ~0xff)
			return -1;

		sha1[i] = val;
		hex++;

		if (hex && *hex == ':')
			hex++;
	}
}

#ifdef USE_OPENSSL
static int check_cert_expiration(X509 *certificate, int days_till_exp, struct ssl_status *status)
{
	ASN1_STRING *tm;
	int offset;
	struct tm stamp;
	float time_left;
	int days_left;
	char timestamp[17] = "";
	char *msg = NULL;
	int st = STATE_UNKNOWN;

	/* Retrieve timestamp of certificate */
	tm = X509_get_notAfter(certificate);

	/* Generate tm structure to process timestamp */
	if (tm->type == V_ASN1_UTCTIME) {
		if (tm->length < 10) {
			asprintf (&msg, "%s\n", _("CRITICAL - Wrong time format in certificate."));
			ssl_status_record(status, STATE_CRITICAL, msg);
			free(msg);
			return status->state;
		} else {
			stamp.tm_year = (tm->data[0] - '0') * 10 + (tm->data[1] - '0');
			if (stamp.tm_year < 50)
				stamp.tm_year += 100;
			offset = 0;
		}
	} else {
		if (tm->length < 12) {
			asprintf (&msg, "%s\n", _("CRITICAL - Wrong time format in certificate."));
			ssl_status_record(status, STATE_CRITICAL, msg);
			free(msg);
			return status->state;
		} else {
			stamp.tm_year =
				(tm->data[0] - '0') * 1000 + (tm->data[1] - '0') * 100 +
				(tm->data[2] - '0') * 10 + (tm->data[3] - '0');
			stamp.tm_year -= 1900;
			offset = 2;
		}
	}
	stamp.tm_mon =
		(tm->data[2 + offset] - '0') * 10 + (tm->data[3 + offset] - '0') - 1;
	stamp.tm_mday =
		(tm->data[4 + offset] - '0') * 10 + (tm->data[5 + offset] - '0');
	stamp.tm_hour =
		(tm->data[6 + offset] - '0') * 10 + (tm->data[7 + offset] - '0');
	stamp.tm_min =
		(tm->data[8 + offset] - '0') * 10 + (tm->data[9 + offset] - '0');
	stamp.tm_sec = 0;
	stamp.tm_isdst = -1;

	time_left = difftime(timegm(&stamp), time(NULL));
	days_left = time_left / 86400;
	snprintf
		(timestamp, 17, "%02d/%02d/%04d %02d:%02d",
		 stamp.tm_mon + 1,
		 stamp.tm_mday, stamp.tm_year + 1900, stamp.tm_hour, stamp.tm_min);

	if (days_left > 0 && days_left <= days_till_exp) {
		asprintf (&msg, _("WARNING - Certificate expires in %d day(s) (%s).\n"), days_left, timestamp);
		st=STATE_WARNING;
	} else if (time_left < 0) {
		asprintf (&msg, _("CRITICAL - Certificate expired on %s.\n"), timestamp);
		st=STATE_CRITICAL;
	} else if (days_left == 0) {
		asprintf (&msg, _("WARNING - Certificate expires today (%s).\n"), timestamp);
		st=STATE_WARNING;
	} else {
		asprintf (&msg, _("OK - Certificate will expire on %s.\n"), timestamp);
		st=STATE_OK;
	}

	ssl_status_record(status, st, msg);
	free(msg);
	return status->state;
}

static int check_cert_fingerprint(X509 *certificate, const char *fingerprint, struct ssl_status *status)
{
	const EVP_MD *digest;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int md_len;
	unsigned char fp[EVP_MAX_MD_SIZE];
	char *msg = NULL;

	hex2sha1(fp, fingerprint);

	digest = EVP_get_digestbyname("sha1");
	X509_digest(certificate, digest, md, &md_len);

	if (!memcmp(md, fp, md_len)) {
		asprintf (&msg, "%s\n", _("OK - Certificate matches fingerprint"));
		ssl_status_record(status, STATE_OK, msg);
		free(msg);
		return status->state;
	}

	asprintf (&msg, "%s\n", _("CRITICAL - Certificate does not match fingerprint"));

	ssl_status_record(status, STATE_CRITICAL, msg);
	free(msg);

	return status->state;
}


#endif


int np_net_ssl_check_cert(int days_till_exp, const char *fingerprint){
#  ifdef USE_OPENSSL
	X509 *certificate=NULL;
	X509_NAME *subj=NULL;
	struct ssl_status st = SSL_STATUS_INIT;

	ssl_status_set(&st, STATE_UNKNOWN, _("UNKNOWN - Unknown certificate check status"));

	certificate=SSL_get_peer_certificate(s);
	if(! certificate){
		printf ("%s\n",_("CRITICAL - Cannot retrieve server certificate."));
		return STATE_CRITICAL;
	}

	/* Extract CN from certificate subject */
	subj=X509_get_subject_name(certificate);

	if(! subj){
		printf ("%s\n",_("CRITICAL - Cannot retrieve certificate subject."));
		return STATE_CRITICAL;
	}
	cnlen = X509_NAME_get_text_by_NID (subj, NID_commonName, cn, sizeof(cn));
	if ( cnlen == -1 )
		strcpy(cn , _("Unknown CN"));

	ssl_status_set(&st, STATE_UNKNOWN, _("UNKNOWN - Unknown certificate check status"));

	if (days_till_exp > 0) {
		check_cert_expiration(certificate, days_till_exp, &st);
	}

	if (fingerprint) {
		check_cert_fingerprint(certificate, fingerprint, &st);

	}
	X509_free (certificate);
	printf("%s\n", st.message);
	ssl_status_free(&st);

	return st.state;
#  else /* ifndef USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#  endif /* USE_OPENSSL */
}

#endif /* HAVE_SSL */
