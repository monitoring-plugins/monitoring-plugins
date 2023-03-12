/*****************************************************************************
*
* Monitoring Plugins SSL utilities
*
* License: GPL
* Copyright (c) 2005-2010 Monitoring Plugins Development Team
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
#include "common.h"
#include "netutils.h"

#ifdef HAVE_SSL
static SSL_CTX *ctx=NULL;
static SSL *s=NULL;

int np_net_ssl_init(int sd) {
	return np_net_ssl_init_with_hostname(sd, NULL);
}

int np_net_ssl_init_with_hostname(int sd, char *host_name) {
	return np_net_ssl_init_with_hostname_and_version(sd, host_name, 0);
}

int np_net_ssl_init_with_hostname_and_version(int sd, char *host_name, int version) {
	return np_net_ssl_init_with_hostname_version_and_cert(sd, host_name, version, NULL, NULL);
}

int np_net_ssl_init_with_hostname_version_and_cert(int sd, char *host_name, int version, char *cert, char *privkey) {
	long options = 0;

	if ((ctx = SSL_CTX_new(TLS_client_method())) == NULL) {
		printf("%s\n", _("CRITICAL - Cannot create SSL context."));
		return STATE_CRITICAL;
	}

	switch (version) {
	case MP_SSLv2: /* SSLv2 protocol */
		printf("%s\n", _("UNKNOWN - SSL protocol version 2 is not supported by your SSL library."));
		return STATE_UNKNOWN;
	case MP_SSLv3: /* SSLv3 protocol */
#if defined(OPENSSL_NO_SSL3)
		printf("%s\n", _("UNKNOWN - SSL protocol version 3 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
		SSL_CTX_set_max_proto_version(ctx, SSL3_VERSION);
		break;
#endif
	case MP_TLSv1: /* TLSv1 protocol */
#if defined(OPENSSL_NO_TLS1)
		printf("%s\n", _("UNKNOWN - TLS protocol version 1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_VERSION);
		break;
#endif
	case MP_TLSv1_1: /* TLSv1.1 protocol */
#if !defined(SSL_OP_NO_TLSv1_1)
		printf("%s\n", _("UNKNOWN - TLS protocol version 1.1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_1_VERSION);
		break;
#endif
	case MP_TLSv1_2: /* TLSv1.2 protocol */
#if !defined(SSL_OP_NO_TLSv1_2)
		printf("%s\n", _("UNKNOWN - TLS protocol version 1.2 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
		break;
#endif
	case MP_TLSv1_2_OR_NEWER:
#if !defined(SSL_OP_NO_TLSv1_1)
		printf("%s\n", _("UNKNOWN - Disabling TLSv1.1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		break;
#endif
	case MP_TLSv1_1_OR_NEWER:
#if !defined(SSL_OP_NO_TLSv1)
		printf("%s\n", _("UNKNOWN - Disabling TLSv1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#else
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
		break;
#endif
	case MP_TLSv1_OR_NEWER:
#if defined(SSL_OP_NO_SSLv3)
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
		break;
#endif
	case MP_SSLv3_OR_NEWER:
#if defined(SSL_OP_NO_SSLv2)
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
		break;
#endif
	}

	if (cert && privkey) {
#ifdef USE_OPENSSL
		if (!SSL_CTX_use_certificate_chain_file(ctx, cert)) {
#elif  USE_GNUTLS
		if (!SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM)) {
#else
#error Unported for unknown SSL library
#endif
			printf ("%s\n", _("CRITICAL - Unable to open certificate chain file!\n"));
			return STATE_CRITICAL;
		}
		SSL_CTX_use_PrivateKey_file(ctx, privkey, SSL_FILETYPE_PEM);
#ifdef USE_OPENSSL
		if (!SSL_CTX_check_private_key(ctx)) {
			printf ("%s\n", _("CRITICAL - Private key does not seem to match certificate!\n"));
			return STATE_CRITICAL;
		}
#endif
	}
#ifdef SSL_OP_NO_TICKET
	options |= SSL_OP_NO_TICKET;
#endif
	SSL_CTX_set_options(ctx, options);
	SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
	if ((s = SSL_new(ctx)) != NULL) {
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
		if (ctx) {
			SSL_CTX_free(ctx);
			ctx=NULL;
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

int np_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn, int days_till_exp_crit){
#  ifdef USE_OPENSSL
	X509_NAME *subj=NULL;
	char timestamp[50] = "";
	char cn[MAX_CN_LENGTH]= "";
	char *tz;

	int cnlen =-1;
	int status=STATE_UNKNOWN;

	ASN1_STRING *tm;
	int offset;
	struct tm stamp;
	float time_left;
	int days_left;
	int time_remaining;
	time_t tm_t;

	if (!certificate) {
		printf("%s\n",_("CRITICAL - Cannot retrieve server certificate."));
		return STATE_CRITICAL;
	}

	/* Extract CN from certificate subject */
	subj=X509_get_subject_name(certificate);

	if (!subj) {
		printf("%s\n",_("CRITICAL - Cannot retrieve certificate subject."));
		return STATE_CRITICAL;
	}
	cnlen = X509_NAME_get_text_by_NID(subj, NID_commonName, cn, sizeof(cn));
	if (cnlen == -1)
		strcpy(cn, _("Unknown CN"));

	/* Retrieve timestamp of certificate */
	tm = X509_get_notAfter(certificate);

	/* Generate tm structure to process timestamp */
	if (tm->type == V_ASN1_UTCTIME) {
		if (tm->length < 10) {
			printf("%s\n", _("CRITICAL - Wrong time format in certificate."));
			return STATE_CRITICAL;
		} else {
			stamp.tm_year = (tm->data[0] - '0') * 10 + (tm->data[1] - '0');
			if (stamp.tm_year < 50)
				stamp.tm_year += 100;
			offset = 0;
		}
	} else {
		if (tm->length < 12) {
			printf("%s\n", _("CRITICAL - Wrong time format in certificate."));
			return STATE_CRITICAL;
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
	stamp.tm_sec =
		(tm->data[10 + offset] - '0') * 10 + (tm->data[11 + offset] - '0');
	stamp.tm_isdst = -1;

	tm_t = timegm(&stamp);
	time_left = difftime(tm_t, time(NULL));
	days_left = time_left / 86400;
	tz = getenv("TZ");
	setenv("TZ", "GMT", 1);
	tzset();
	strftime(timestamp, 50, "%c %z", localtime(&tm_t));
	if (tz)
		setenv("TZ", tz, 1);
	else
		unsetenv("TZ");
	tzset();

	if (days_left > 0 && days_left <= days_till_exp_warn) {
		printf (_("%s - Certificate '%s' expires in %d day(s) (%s).\n"), (days_left>days_till_exp_crit)?"WARNING":"CRITICAL", cn, days_left, timestamp);
		if (days_left > days_till_exp_crit)
			status = STATE_WARNING;
		else
			status = STATE_CRITICAL;
	} else if (days_left == 0 && time_left > 0) {
		if (time_left >= 3600)
			time_remaining = (int) time_left / 3600;
		else
			time_remaining = (int) time_left / 60;

		printf (_("%s - Certificate '%s' expires in %u %s (%s)\n"),
			(days_left>days_till_exp_crit) ? "WARNING" : "CRITICAL", cn, time_remaining,
			time_left >= 3600 ? "hours" : "minutes", timestamp);

		if ( days_left > days_till_exp_crit)
			status = STATE_WARNING;
		else
			status = STATE_CRITICAL;
	} else if (time_left < 0) {
		printf(_("CRITICAL - Certificate '%s' expired on %s.\n"), cn, timestamp);
		status=STATE_CRITICAL;
	} else if (days_left == 0) {
		printf (_("%s - Certificate '%s' just expired (%s).\n"), (days_left>days_till_exp_crit)?"WARNING":"CRITICAL", cn, timestamp);
		if (days_left > days_till_exp_crit)
			status = STATE_WARNING;
		else
			status = STATE_CRITICAL;
	} else {
		printf(_("OK - Certificate '%s' will expire on %s.\n"), cn, timestamp);
		status = STATE_OK;
	}
	X509_free(certificate);
	return status;
#  else /* ifndef USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#  endif /* USE_OPENSSL */
}

int np_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit){
#  ifdef USE_OPENSSL
	X509 *certificate = NULL;
	certificate=SSL_get_peer_certificate(s);
	return(np_net_ssl_check_certificate(certificate, days_till_exp_warn, days_till_exp_crit));
#  else /* ifndef USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#  endif /* USE_OPENSSL */
}


#endif /* HAVE_SSL */
