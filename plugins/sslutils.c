/*****************************************************************************
* 
* Nagios plugins SSL utilities
* 
* License: GPL
* Copyright (c) 2005-2007 Nagios Plugins Development Team
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

#define LOCAL_TIMEOUT_ALARM_HANDLER
#include "common.h"
#include "netutils.h"

#ifdef HAVE_SSL
static SSL_CTX *c=NULL;
static SSL *s=NULL;
static int initialized=0;

int np_net_ssl_init (int sd) {
		return np_net_ssl_init_with_hostname(sd, NULL);
}

int np_net_ssl_init_with_hostname (int sd, char *host_name) {
		if (!initialized) {
			/* Initialize SSL context */
			SSLeay_add_ssl_algorithms ();
			SSL_load_error_strings ();
			OpenSSL_add_all_algorithms ();
			initialized = 1;
		}
		if ((c = SSL_CTX_new (SSLv23_client_method ())) == NULL) {
				printf ("%s\n", _("CRITICAL - Cannot create SSL context."));
				return STATE_CRITICAL;
		}
		if ((s = SSL_new (c)) != NULL){
#ifdef SSL_set_tlsext_host_name
				if (host_name != NULL)
					SSL_set_tlsext_host_name(s, host_name);
#endif
				SSL_set_fd (s, sd);
				if (SSL_connect(s) == 1){
						return OK;
				} else {
						printf ("%s\n", _("CRITICAL - Cannot make SSL connection "));
#  ifdef USE_OPENSSL /* XXX look into ERR_error_string */
						ERR_print_errors_fp (stdout);
#  endif /* USE_OPENSSL */
				}
		} else {
				printf ("%s\n", _("CRITICAL - Cannot initiate SSL handshake."));
		}
		return STATE_CRITICAL;
}

void np_net_ssl_cleanup (){
		if(s){
#ifdef SSL_set_tlsext_host_name
				SSL_set_tlsext_host_name(s, NULL);
#endif
				SSL_shutdown (s);
				SSL_free (s);
				if(c) {
					SSL_CTX_free (c);
					c=NULL;
				}
				s=NULL;
		}
}

int np_net_ssl_write(const void *buf, int num){
	return SSL_write(s, buf, num);
}

int np_net_ssl_read(void *buf, int num){
	return SSL_read(s, buf, num);
}

int np_net_ssl_check_cert(int days_till_exp){
#  ifdef USE_OPENSSL
	X509 *certificate=NULL;
	ASN1_STRING *tm;
	int offset;
	struct tm stamp;
	float time_left;
	int days_left;
	char timestamp[17] = "";

	certificate=SSL_get_peer_certificate(s);
	if(! certificate){
		printf ("%s\n",_("CRITICAL - Cannot retrieve server certificate."));
		return STATE_CRITICAL;
	}

	/* Retrieve timestamp of certificate */
	tm = X509_get_notAfter (certificate);

	/* Generate tm structure to process timestamp */
	if (tm->type == V_ASN1_UTCTIME) {
		if (tm->length < 10) {
			printf ("%s\n", _("CRITICAL - Wrong time format in certificate."));
			return STATE_CRITICAL;
		} else {
			stamp.tm_year = (tm->data[0] - '0') * 10 + (tm->data[1] - '0');
			if (stamp.tm_year < 50)
				stamp.tm_year += 100;
			offset = 0;
		}
	} else {
		if (tm->length < 12) {
			printf ("%s\n", _("CRITICAL - Wrong time format in certificate."));
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
	stamp.tm_sec = 0;
	stamp.tm_isdst = -1;

	time_left = difftime(timegm(&stamp), time(NULL));
	days_left = time_left / 86400;
	snprintf
		(timestamp, 17, "%02d/%02d/%04d %02d:%02d",
		 stamp.tm_mon + 1,
		 stamp.tm_mday, stamp.tm_year + 1900, stamp.tm_hour, stamp.tm_min);

	if (days_left > 0 && days_left <= days_till_exp) {
		printf (_("WARNING - Certificate expires in %d day(s) (%s).\n"), days_left, timestamp);
		return STATE_WARNING;
	} else if (time_left < 0) {
		printf (_("CRITICAL - Certificate expired on %s.\n"), timestamp);
		return STATE_CRITICAL;
	} else if (days_left == 0) {
		printf (_("WARNING - Certificate expires today (%s).\n"), timestamp);
		return STATE_WARNING;
	}

	printf (_("OK - Certificate will expire on %s.\n"), timestamp);
	X509_free (certificate);
	return STATE_OK;
#  else /* ifndef USE_OPENSSL */
	printf ("%s\n", _("WARNING - Plugin does not support checking certificates."));
	return STATE_WARNING;
#  endif /* USE_OPENSSL */
}

#endif /* HAVE_SSL */
