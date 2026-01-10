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

/* process_request and wrapper macros */
#define process_tcp_request(addr, port, sbuf, rbuf, rsize)                                         \
	process_request(addr, port, IPPROTO_TCP, sbuf, rbuf, rsize)
#define process_udp_request(addr, port, sbuf, rbuf, rsize)                                         \
	process_request(addr, port, IPPROTO_UDP, sbuf, rbuf, rsize)
mp_state_enum process_tcp_request2(const char *server_address, int server_port,
								   const char *send_buffer, char *recv_buffer, int recv_size);
mp_state_enum process_request(const char *server_address, int server_port, int proto,
							  const char *send_buffer, char *recv_buffer, int recv_size);

/* my_connect and wrapper macros */
#define my_tcp_connect(addr, port, s) np_net_connect(addr, port, s, IPPROTO_TCP)
#define my_udp_connect(addr, port, s) np_net_connect(addr, port, s, IPPROTO_UDP)
mp_state_enum np_net_connect(const char *host_name, int port, int *socketDescriptor, int proto);

/* send_request and wrapper macros */
#define send_tcp_request(s, sbuf, rbuf, rsize) send_request(s, IPPROTO_TCP, sbuf, rbuf, rsize)
#define send_udp_request(s, sbuf, rbuf, rsize) send_request(s, IPPROTO_UDP, sbuf, rbuf, rsize)
mp_state_enum send_request(int socket, int proto, const char *send_buffer, char *recv_buffer,
						   int recv_size);

/* "is_*" wrapper macros and functions */
bool is_host(const char *);
bool is_addr(const char *);
bool dns_lookup(const char *, struct sockaddr_storage *, int);
void host_or_die(const char *str);
#define resolve_host_or_addr(addr, family) dns_lookup(addr, NULL, family)
#define is_inet_addr(addr)                 resolve_host_or_addr(addr, AF_INET)
#	define is_inet6_addr(addr) resolve_host_or_addr(addr, AF_INET6)
#	define is_hostname(addr)   resolve_host_or_addr(addr, address_family)

extern unsigned int socket_timeout;
extern mp_state_enum socket_timeout_state;
extern mp_state_enum econn_refuse_state;
extern bool was_refused;
extern int address_family;

void socket_timeout_alarm_handler(int) __attribute__((noreturn));

/* SSL-Related functionality */
#ifdef HAVE_SSL
#	define MP_SSLv2            1
#	define MP_SSLv3            2
#	define MP_TLSv1            3
#	define MP_TLSv1_1          4
#	define MP_TLSv1_2          5
#	define MP_SSLv2_OR_NEWER   6
#	define MP_SSLv3_OR_NEWER   7
#	define MP_TLSv1_OR_NEWER   8
#	define MP_TLSv1_1_OR_NEWER 9
#	define MP_TLSv1_2_OR_NEWER 10
/* maybe this could be merged with the above np_net_connect, via some flags */
int np_net_ssl_init(int socket);
int np_net_ssl_init_with_hostname(int socket, char *host_name);
int np_net_ssl_init_with_hostname_and_version(int socket, char *host_name, int version);
int np_net_ssl_init_with_hostname_version_and_cert(int socket, char *host_name, int version,
												   char *cert, char *privkey);
void np_net_ssl_cleanup(void);
int np_net_ssl_write(const void *buf, int num);
int np_net_ssl_read(void *buf, int num);

typedef enum {
	ALL_OK,
	NO_SERVER_CERTIFICATE_PRESENT,
	UNABLE_TO_RETRIEVE_CERTIFICATE_SUBJECT,
	WRONG_TIME_FORMAT_IN_CERTIFICATE,
} retrieve_expiration_date_errors;

typedef struct {
	double remaining_seconds;
	retrieve_expiration_date_errors errors;
} retrieve_expiration_time_result;

typedef struct {
	mp_state_enum result_state;
	double remaining_seconds;
	retrieve_expiration_date_errors errors;
} net_ssl_check_cert_result;
net_ssl_check_cert_result np_net_ssl_check_cert2(int days_till_exp_warn, int days_till_exp_crit);

mp_state_enum np_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit);
mp_subcheck mp_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit);
#endif /* HAVE_SSL */
#endif /* _NETUTILS_H_ */
