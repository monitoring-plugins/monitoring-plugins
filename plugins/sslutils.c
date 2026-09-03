/*****************************************************************************
 *
 * Monitoring Plugins SSL utilities
 *
 * License: GPL
 * Copyright (c) 2005-2024 Monitoring Plugins Development Team
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

#include "output.h"
#define MAX_CN_LENGTH 256
#include "common.h"
#include "netutils.h"
#include "../lib/monitoringplug.h"
#include "states.h"

#ifdef HAVE_SSL
static SSL_CTX *ctx = NULL;
static SSL *SSL_context = NULL;

int mopl_net_tls_init(int socket) { return mopl_net_tls_init_with_hostname(socket, NULL); }

int mopl_net_tls_init_with_hostname(int socket, char *host_name) {
	return mopl_net_tls_init_with_hostname_and_version(socket, host_name, MOPL_NET_TLS_DEFAULT_VERSION);
}

int mopl_net_tls_init_with_hostname_and_version(int socket, char *host_name,
												mopl_tls_version version) {
	return mopl_net_tls_init_with_hostname_version_and_cert(socket, host_name, version, NULL, NULL);
}

#	ifdef MOPL_USE_OPENSSL
int mopl_net_asn1_time_to_time_t(const ASN1_TIME *asn1_time, time_t *out) {
	struct tm time_marker = {};
	if (!ASN1_TIME_to_tm(asn1_time, &time_marker)) {
		return 0;
	}
	*out = timegm(&time_marker);
	if (*out == (time_t)-1) {
		return 0;
	}
	return 1;
}

void mopl_net_format_timestamp(time_t timestamp, char *buf, size_t buflen) {
	char *time_zone_setting = getenv("TZ");
	setenv("TZ", "GMT", 1);
	tzset();
	strftime(buf, buflen, "%c %z", localtime(&timestamp));
	if (time_zone_setting) {
		setenv("TZ", time_zone_setting, 1);
	} else {
		unsetenv("TZ");
	}
	tzset();
}
#	endif /* MOPL_USE_OPENSSL */

int mopl_net_tls_init_with_hostname_version_and_cert(int socket, char *host_name,
													 mopl_tls_version version, char *cert,
													 char *privkey) {
	unsigned long options = 0;

	if ((ctx = SSL_CTX_new(TLS_client_method())) == NULL) {
		printf("%s\n", _("CRITICAL - Cannot create SSL context."));
		return STATE_CRITICAL;
	}

	switch (version) {
	case MOPL_NET_SSLv2: /* SSLv2 protocol */
		printf("%s\n", _("UNKNOWN - SSL protocol version 2 is not supported by your SSL library."));
		return STATE_UNKNOWN;
	case MOPL_NET_SSLv3: /* SSLv3 protocol */
#	if defined(OPENSSL_NO_SSL3)
		printf("%s\n", _("UNKNOWN - SSL protocol version 3 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
		SSL_CTX_set_max_proto_version(ctx, SSL3_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1: /* TLSv1 protocol */
#	if defined(OPENSSL_NO_TLS1)
		printf("%s\n", _("UNKNOWN - TLS protocol version 1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1_1: /* TLSv1.1 protocol */
#	if !defined(SSL_OP_NO_TLSv1_1)
		printf("%s\n",
			   _("UNKNOWN - TLS protocol version 1.1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_1_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1_2: /* TLSv1.2 protocol */
#	if !defined(SSL_OP_NO_TLSv1_2)
		printf("%s\n",
			   _("UNKNOWN - TLS protocol version 1.2 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1_2_OR_NEWER:
#	if !defined(SSL_OP_NO_TLSv1_1)
		printf("%s\n", _("UNKNOWN - Disabling TLSv1.1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1_1_OR_NEWER:
#	if !defined(SSL_OP_NO_TLSv1)
		printf("%s\n", _("UNKNOWN - Disabling TLSv1 is not supported by your SSL library."));
		return STATE_UNKNOWN;
#	else
		SSL_CTX_set_min_proto_version(ctx, TLS1_1_VERSION);
		break;
#	endif
	case MOPL_NET_TLSv1_OR_NEWER:
#	if defined(SSL_OP_NO_SSLv3)
		SSL_CTX_set_min_proto_version(ctx, TLS1_VERSION);
		break;
#	endif
	case MOPL_NET_SSLv2_OR_NEWER:
#	if defined(SSL_OP_NO_SSLv2)
		SSL_CTX_set_min_proto_version(ctx, SSL2_VERSION);
		break;
#	endif
	case MOPL_NET_SSLv3_OR_NEWER:
#	if defined(SSL_OP_NO_SSLv3)
		SSL_CTX_set_min_proto_version(ctx, SSL3_VERSION);
		break;
#	endif
	case MOPL_NET_TLS_DEFAULT_VERSION: {
			// do nothing special here
	}
	}

	if (cert && privkey) {
#	ifdef MOPL_USE_OPENSSL
		if (!SSL_CTX_use_certificate_chain_file(ctx, cert)) {
#	elif USE_GNUTLS
		if (!SSL_CTX_use_certificate_file(ctx, cert, SSL_FILETYPE_PEM)) {
#	else
#		error Unported for unknown SSL library
#	endif
			printf("%s\n", _("CRITICAL - Unable to open certificate chain file!\n"));
			return STATE_CRITICAL;
		}
		SSL_CTX_use_PrivateKey_file(ctx, privkey, SSL_FILETYPE_PEM);
#	ifdef MOPL_USE_OPENSSL
		if (!SSL_CTX_check_private_key(ctx)) {
			printf("%s\n", _("CRITICAL - Private key does not seem to match certificate!\n"));
			return STATE_CRITICAL;
		}
#	endif
	}
#	ifdef SSL_OP_NO_TICKET
	options |= SSL_OP_NO_TICKET;
#	endif
	SSL_CTX_set_options(ctx, options);
	SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
	if ((SSL_context = SSL_new(ctx)) != NULL) {
#	ifdef SSL_set_tlsext_host_name
		if (host_name != NULL) {
			SSL_set_tlsext_host_name(SSL_context, host_name);
		}
#	endif
		SSL_set_fd(SSL_context, socket);
		if (SSL_connect(SSL_context) == 1) {
			return OK;
		}
		printf("%s\n", _("CRITICAL - Cannot make SSL connection."));
#	ifdef MOPL_USE_OPENSSL /* XXX look into ERR_error_string */
		ERR_print_errors_fp(stdout);
#	endif /* MOPL_USE_OPENSSL */

	} else {
		printf("%s\n", _("CRITICAL - Cannot initiate SSL handshake."));
	}
	return STATE_CRITICAL;
}

void mopl_net_tls_cleanup(void) {
	if (SSL_context) {
#	ifdef SSL_set_tlsext_host_name
		SSL_set_tlsext_host_name(SSL_context, NULL);
#	endif
		SSL_shutdown(SSL_context);
		SSL_free(SSL_context);
		if (ctx) {
			SSL_CTX_free(ctx);
			ctx = NULL;
		}
		SSL_context = NULL;
	}
}

int mopl_net_ssl_write(const void *buf, int num) { return SSL_write(SSL_context, buf, num); }

int mopl_net_ssl_read(void *buf, int num) { return SSL_read(SSL_context, buf, num); }

mp_state_enum np_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn,
										   int days_till_exp_crit) {
#	ifdef MOPL_USE_OPENSSL
	if (!certificate) {
		printf("%s\n", _("CRITICAL - No server certificate present to inspect."));
		return STATE_CRITICAL;
	}

	/* Extract CN from certificate subject */
	X509_NAME *subj = X509_get_subject_name(certificate);

	if (!subj) {
		printf("%s\n", _("CRITICAL - Cannot retrieve certificate subject."));
		return STATE_CRITICAL;
	}

	char common_name[MAX_CN_LENGTH] = "";
	int cnlen = X509_NAME_get_text_by_NID(subj, NID_commonName, common_name, sizeof(common_name));
	if (cnlen == -1) {
		strcpy(common_name, _("Unknown CN"));
	}

	/* Retrieve timestamp of certificate */
	const ASN1_TIME *asn1_not_after = X509_get_notAfter(certificate);
	time_t expiry_time;
	if (!mopl_net_asn1_time_to_time_t(asn1_not_after, &expiry_time)) {
		printf("%s\n", _("CRITICAL - Wrong time format in certificate."));
		return STATE_CRITICAL;
	}
	double time_left = difftime(expiry_time, time(NULL));
	int days_left = (int)(time_left / 86400);
	char timestamp[50] = "";
	mopl_net_format_timestamp(expiry_time, timestamp, sizeof(timestamp));

	int time_remaining;
	mp_state_enum status = STATE_UNKNOWN;
	if (days_left > 0 && days_left <= days_till_exp_warn) {
		printf(_("%s - Certificate '%s' expires in %d day(s) (%s).\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", common_name, days_left,
			   timestamp);
		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else if (days_left == 0 && time_left > 0) {
		if (time_left >= 3600) {
			time_remaining = (int)(time_left / 3600);
		} else {
			time_remaining = (int)(time_left / 60);
		}

		printf(_("%s - Certificate '%s' expires in %u %s (%s)\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", common_name,
			   time_remaining, time_left >= 3600 ? "hours" : "minutes", timestamp);

		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else if (time_left < 0) {
		printf(_("CRITICAL - Certificate '%s' expired on %s.\n"), common_name, timestamp);
		status = STATE_CRITICAL;
	} else if (days_left == 0) {
		printf(_("%s - Certificate '%s' just expired (%s).\n"),
			   (days_left > days_till_exp_crit) ? "WARNING" : "CRITICAL", common_name, timestamp);
		if (days_left > days_till_exp_crit) {
			status = STATE_WARNING;
		} else {
			status = STATE_CRITICAL;
		}
	} else {
		printf(_("OK - Certificate '%s' will expire on %s.\n"), common_name, timestamp);
		status = STATE_OK;
	}
	X509_free(certificate);
	return status;
#	else  /* ifndef MOPL_USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#	endif /* MOPL_USE_OPENSSL */
}

mopl_net_retrieve_expiration_time_result np_net_ssl_get_cert_expiration(X509 *certificate) {
#	ifdef MOPL_USE_OPENSSL
	mopl_net_retrieve_expiration_time_result result = {
		.errors = ALL_OK,
		.remaining_seconds = 0,
	};

	if (!certificate) {
		result.errors = NO_SERVER_CERTIFICATE_PRESENT;
		return result;
	}

	/* Extract CN from certificate subject */
	X509_NAME *subj = X509_get_subject_name(certificate);

	if (!subj) {
		result.errors = UNABLE_TO_RETRIEVE_CERTIFICATE_SUBJECT;
		return result;
	}

	char common_name[MAX_CN_LENGTH] = "";
	int cnlen = X509_NAME_get_text_by_NID(subj, NID_commonName, common_name, sizeof(common_name));
	if (cnlen == -1) {
		strcpy(common_name, _("Unknown CN"));
	}

	/* Retrieve timestamp of certificate */
	const ASN1_TIME *asn1_not_after = X509_get_notAfter(certificate);
	time_t expiry_time;
	if (!mopl_net_asn1_time_to_time_t(asn1_not_after, &expiry_time)) {
		result.errors = WRONG_TIME_FORMAT_IN_CERTIFICATE;
		return result;
	}
	double time_left = difftime(expiry_time, time(NULL));
	result.remaining_seconds = time_left;

	X509_free(certificate);

	return result;
#	else  /* ifndef MOPL_USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#	endif /* MOPL_USE_OPENSSL */
}

mopl_net_ssl_check_cert_result mopl_net_ssl_check_cert2(unsigned int days_till_exp_warn,
														unsigned int days_till_exp_crit) {
#	ifdef MOPL_USE_OPENSSL
	X509 *certificate = NULL;
	certificate = SSL_get_peer_certificate(SSL_context);

	mopl_net_retrieve_expiration_time_result expiration_date =
		np_net_ssl_get_cert_expiration(certificate);

	mopl_net_ssl_check_cert_result result = {
		.result_state = STATE_UNKNOWN,
		.remaining_seconds = expiration_date.remaining_seconds,
		.errors = expiration_date.errors,
	};

	if (expiration_date.errors == ALL_OK) {
		// got a valid expiration date
		unsigned int remaining_days = result.remaining_seconds / 86400;

		if (remaining_days < days_till_exp_crit) {
			result.result_state = STATE_CRITICAL;
		} else if (remaining_days < days_till_exp_warn) {
			result.result_state = STATE_WARNING;
		} else {
			result.result_state = STATE_OK;
		}
	}

	return result;

#	else  /* ifndef MOPL_USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#	endif /* MOPL_USE_OPENSSL */
}

mp_state_enum mopl_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit) {
#	ifdef MOPL_USE_OPENSSL
	X509 *certificate = NULL;
	certificate = SSL_get_peer_certificate(SSL_context);
	return (np_net_ssl_check_certificate(certificate, days_till_exp_warn, days_till_exp_crit));
#	else  /* ifndef MOPL_USE_OPENSSL */
	printf("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#	endif /* MOPL_USE_OPENSSL */
}

mp_subcheck mp_net_ssl_check_certificate(X509 *certificate, int days_till_exp_warn,
										 int days_till_exp_crit) {
	mp_subcheck sc_cert = mp_subcheck_init();
#	ifdef MOPL_USE_OPENSSL
	if (!certificate) {
		xasprintf(&sc_cert.output, _("No server certificate present to inspect"));
		sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		return sc_cert;
	}

	/* Extract CN from certificate subject */
	X509_NAME *subj = X509_get_subject_name(certificate);

	if (!subj) {
		xasprintf(&sc_cert.output, _("Cannot retrieve certificate subject"));
		sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		return sc_cert;
	}

	char commonName[MAX_CN_LENGTH] = "";
	int cnlen = X509_NAME_get_text_by_NID(subj, NID_commonName, commonName, sizeof(commonName));
	if (cnlen == -1) {
		strcpy(commonName, _("Unknown CN"));
	}

	/* Retrieve timestamp of certificate */
	const ASN1_TIME *asn1_not_after = X509_get_notAfter(certificate);
	time_t expiry_time;
	if (!mopl_net_asn1_time_to_time_t(asn1_not_after, &expiry_time)) {
		xasprintf(&sc_cert.output, _("Wrong time format in certificate"));
		sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		return sc_cert;
	}
	double time_left = difftime(expiry_time, time(NULL));
	int days_left = (int)(time_left / 86400);
	char timestamp[50] = "";
	mopl_net_format_timestamp(expiry_time, timestamp, sizeof(timestamp));

	int time_remaining;
	if (days_left > 0 && days_left <= days_till_exp_warn) {
		xasprintf(&sc_cert.output, _("Certificate '%s' expires in %d day(s) (%s)"), commonName,
				  days_left, timestamp);
		if (days_left > days_till_exp_crit) {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_WARNING);
		} else {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		}
	} else if (days_left == 0 && time_left > 0) {
		if (time_left >= 3600) {
			time_remaining = (int)time_left / 3600;
		} else {
			time_remaining = (int)time_left / 60;
		}

		xasprintf(&sc_cert.output, _("Certificate '%s' expires in %u %s (%s)"), commonName,
				  time_remaining, time_left >= 3600 ? "hours" : "minutes", timestamp);

		if (days_left > days_till_exp_crit) {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_WARNING);
		} else {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		}
	} else if (time_left < 0) {
		xasprintf(&sc_cert.output, _("Certificate '%s' expired on %s"), commonName, timestamp);
		sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
	} else if (days_left == 0) {
		xasprintf(&sc_cert.output, _("Certificate '%s' just expired (%s)"), commonName, timestamp);
		if (days_left > days_till_exp_crit) {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_WARNING);
		} else {
			sc_cert = mp_set_subcheck_state(sc_cert, STATE_CRITICAL);
		}
	} else {
		xasprintf(&sc_cert.output, _("Certificate '%s' will expire on %s"), commonName, timestamp);
		sc_cert = mp_set_subcheck_state(sc_cert, STATE_OK);
	}
	X509_free(certificate);
	return sc_cert;
#	else  /* ifndef MOPL_USE_OPENSSL */
	xasprintf(&sc_cert.output, _("Plugin does not support checking certificates"));
	sc_cert = mp_set_subcheck_state(sc_cert, STATE_WARNING);
	return sc_cert;
#	endif /* MOPL_USE_OPENSSL */
}
#endif /* HAVE_SSL */
