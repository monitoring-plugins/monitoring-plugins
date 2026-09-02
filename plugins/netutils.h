/*****************************************************************************
 *
 * Monitoring Plugins net utilities include file
 *
 * License: GPL
 * Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
 * Copyright (c) 2003-2007 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains common include files and function definitions
 * used in many of the plugins.
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

#ifndef _NETUTILS_H_
#define _NETUTILS_H_

#include "output.h"
#include "states.h"
#include "utils.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef HAVE_SYS_UN_H
#	include <sys/un.h>
#	ifndef UNIX_PATH_MAX
/* linux uses this, on sun it's hard-coded at 108 without a define, on BSD at 104 */
#		define UNIX_PATH_MAX 104
#	endif /* UNIX_PATH_MAX */
#endif     /* HAVE_SYS_UN_H */

#ifndef HOST_MAX_BYTES
#	define HOST_MAX_BYTES 255
#endif

/* process_requests */
mp_state_enum mopl_net_process_request(const char *server_address, int server_port, int proto,
									   const char *send_buffer, char *recv_buffer, int recv_size);
mp_state_enum mopl_net_process_tcp_request(const char *server_address, int server_port,
										   const char *send_buffer, char *recv_buffer,
										   int recv_size);

/* net_connect and wrapper macros */
mp_state_enum mopl_net_connect(const char *host_name, int port, int *socketDescriptor, int proto);
mp_state_enum mopl_net_udp_connect(const char *host_name, int port, int *socketDescriptor);
mp_state_enum mopl_net_tcp_connect(const char *host_name, int port, int *socketDescriptor);

/* "is_*" wrapper macros and functions */
bool mopl_net_is_host(const char *);
bool mopl_net_dns_lookup(const char *, struct sockaddr_storage *, int);
void mopl_net_host_or_die(const char *str);
bool mopl_net_is_inet_addr(const char *addr);
bool mopl_net_is_inet6_addr(const char *addr);
bool mopl_net_is_hostname(const char *addr);

extern unsigned int socket_timeout;
extern mp_state_enum socket_timeout_state;
extern mp_state_enum econn_refuse_state;
extern bool was_refused;
extern int address_family;

void socket_timeout_alarm_handler(int) __attribute__((noreturn));

/* SSL-Related functionality */
#ifdef HAVE_SSL
typedef enum {
	MOPL_NET_SSLv2,
	MOPL_NET_SSLv3,
	MOPL_NET_TLSv1,
	MOPL_NET_TLSv1_1,
	MOPL_NET_TLSv1_2,
	MOPL_NET_SSLv2_OR_NEWER,
	MOPL_NET_SSLv3_OR_NEWER,
	MOPL_NET_TLSv1_OR_NEWER,
	MOPL_NET_TLSv1_1_OR_NEWER,
	MOPL_NET_TLSv1_2_OR_NEWER,
	MOPL_NET_TLS_DEFAULT_VERSION,
} mopl_tls_version;

int mopl_net_tls_init(int socket);
int mopl_net_tls_init_with_hostname(int socket, char *host_name);
int mopl_net_tls_init_with_hostname_and_version(int socket, char *host_name, mopl_tls_version version);
int mopl_net_tls_init_with_hostname_version_and_cert(int socket, char *host_name, mopl_tls_version version,
													 char *cert, char *privkey);
void mopl_net_tls_cleanup(void);
int mopl_net_ssl_write(const void *buf, int num);
int mopl_net_ssl_read(void *buf, int num);

typedef enum {
	ALL_OK,
	NO_SERVER_CERTIFICATE_PRESENT,
	UNABLE_TO_RETRIEVE_CERTIFICATE_SUBJECT,
	WRONG_TIME_FORMAT_IN_CERTIFICATE,
} mopl_net_retrieve_expiration_date_errors;

typedef struct {
	double remaining_seconds;
	mopl_net_retrieve_expiration_date_errors errors;
} mopl_net_retrieve_expiration_time_result;

typedef struct {
	mp_state_enum result_state;
	double remaining_seconds;
	mopl_net_retrieve_expiration_date_errors errors;
} mopl_net_ssl_check_cert_result;
mopl_net_ssl_check_cert_result mopl_net_ssl_check_cert2(unsigned int days_till_exp_warn,
														unsigned int days_till_exp_crit);

mp_state_enum np_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit);
mp_subcheck mp_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit);
#endif /* HAVE_SSL */
#endif /* _NETUTILS_H_ */
