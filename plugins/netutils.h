/*****************************************************************************
* 
* Nagios plugins net utilities include file
* 
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
* Copyright (c) 2003-2007 Nagios Plugins Development Team
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

#include "common.h"
#include "utils.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef HAVE_SYS_UN_H
# include <sys/un.h>
# ifndef UNIX_PATH_MAX
   /* linux uses this, on sun it's hard-coded at 108 without a define */
#  define UNIX_PATH_MAX 108
# endif /* UNIX_PATH_MAX */
#endif /* HAVE_SYS_UN_H */

/* process_request and wrapper macros */
#define process_tcp_request(addr, port, sbuf, rbuf, rsize) \
	process_request(addr, port, IPPROTO_TCP, sbuf, rbuf, rsize)
#define process_udp_request(addr, port, sbuf, rbuf, rsize) \
	process_request(addr, port, IPPROTO_UDP, sbuf, rbuf, rsize)
int process_tcp_request2 (const char *address, int port,
  const char *sbuffer, char *rbuffer, int rsize);
int process_request (const char *address, int port, int proto,
  const char *sbuffer, char *rbuffer, int rsize);

/* my_connect and wrapper macros */
#define my_tcp_connect(addr, port, s) np_net_connect(addr, port, s, IPPROTO_TCP)
#define my_udp_connect(addr, port, s) np_net_connect(addr, port, s, IPPROTO_UDP)
int np_net_connect(const char *address, int port, int *sd, int proto);

/* send_request and wrapper macros */
#define send_tcp_request(s, sbuf, rbuf, rsize) \
	send_request(s, IPPROTO_TCP, sbuf, rbuf, rsize)
#define send_udp_request(s, sbuf, rbuf, rsize) \
	send_request(s, IPPROTO_UDP, sbuf, rbuf, rsize)
int send_request (int sd, int proto, const char *send_buffer, char *recv_buffer, int recv_size);


/* "is_*" wrapper macros and functions */
int is_host (const char *);
int is_addr (const char *);
int resolve_host_or_addr (const char *, int);
void host_or_die(const char *str);
#define is_inet_addr(addr) resolve_host_or_addr(addr, AF_INET)
#ifdef USE_IPV6
#  define is_inet6_addr(addr) resolve_host_or_addr(addr, AF_INET6)
#  define is_hostname(addr) resolve_host_or_addr(addr, address_family)
#else
#  define is_hostname(addr) resolve_host_or_addr(addr, AF_INET)
#endif

extern unsigned int socket_timeout;
extern unsigned int socket_timeout_state;
extern int econn_refuse_state;
extern int was_refused;
extern int address_family;

RETSIGTYPE socket_timeout_alarm_handler (int) __attribute__((noreturn));

/* SSL-Related functionality */
#ifdef HAVE_SSL
/* maybe this could be merged with the above np_net_connect, via some flags */
int np_net_ssl_init(int sd);
int np_net_ssl_init_with_hostname(int sd, char *host_name);
int np_net_ssl_init_with_hostname_and_version(int sd, char *host_name, int version);
int np_net_ssl_init_with_hostname_version_and_cert(int sd, char *host_name, int version, char *cert, char *privkey);
void np_net_ssl_cleanup();
int np_net_ssl_write(const void *buf, int num);
int np_net_ssl_read(void *buf, int num);
int np_net_ssl_check_cert(int days_till_exp_warn, int days_till_exp_crit);
#endif /* HAVE_SSL */

#endif /* _NETUTILS_H_ */
