/****************************************************************************
*
* Nagios plugins network utilities
*
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
*
* Last Modified: $Date$
*
* Description:
*
* This file contains commons functions used in many of the plugins.
*
* License Information:
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* $Id$
*
****************************************************************************/

#include "common.h"
#include "netutils.h"

unsigned int socket_timeout = DEFAULT_SOCKET_TIMEOUT; 
int econn_refuse_state = STATE_CRITICAL;
int was_refused = FALSE;
int address_family = AF_UNSPEC;

static int my_connect(const char *address, int port, int *sd, int proto);
/* handles socket timeouts */
void
socket_timeout_alarm_handler (int sig)
{
	if (sig == SIGALRM)
		printf (_("CRITICAL - Socket timeout after %d seconds\n"), socket_timeout);
	else
		printf (_("CRITICAL - Abnormal timeout after %d seconds\n"), socket_timeout);

	exit (STATE_CRITICAL);
}


/* connects to a host on a specified TCP port, sends a string,
   and gets a response */
int
process_tcp_request (const char *server_address, int server_port,
	const char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;

	result = process_request (server_address, server_port,
			IPPROTO_TCP, send_buffer, recv_buffer, recv_size);

	return result;
}


/* connects to a host on a specified UDP port, sends a string, and gets a
    response */
int
process_udp_request (const char *server_address, int server_port,
	const char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;

	result = process_request (server_address, server_port,
			IPPROTO_UDP, send_buffer, recv_buffer, recv_size);

	return result;
}



/* connects to a host on a specified tcp port, sends a string, and gets a 
	 response. loops on select-recv until timeout or eof to get all of a 
	 multi-packet answer */
int
process_tcp_request2 (const char *server_address, int server_port,
	const char *send_buffer, char *recv_buffer, int recv_size)
{

	int result;
	int send_result;
	int recv_result;
	int sd;
	struct timeval tv;
	fd_set readfds;
	int recv_length = 0;

	result = my_connect (server_address, server_port, &sd, IPPROTO_TCP);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result<0 || (size_t)send_result!=strlen(send_buffer)) {
		printf (_("Send failed\n"));
		result = STATE_WARNING;
	}

	while (1) {
		/* wait up to the number of seconds for socket timeout
		   minus one for data from the host */
		tv.tv_sec = socket_timeout - 1;
		tv.tv_usec = 0;
		FD_ZERO (&readfds);
		FD_SET (sd, &readfds);
		select (sd + 1, &readfds, NULL, NULL, &tv);

		/* make sure some data has arrived */
		if (!FD_ISSET (sd, &readfds)) {	/* it hasn't */
			if (!recv_length) {
				strcpy (recv_buffer, "");
				printf (_("No data was received from host!\n"));
				result = STATE_WARNING;
			}
			else {										/* this one failed, but previous ones worked */
				recv_buffer[recv_length] = 0;
			}
			break;
		}
		else {											/* it has */
			recv_result =
				recv (sd, recv_buffer + recv_length, 
					(size_t)recv_size - recv_length - 1, 0);
			if (recv_result == -1) {
				/* recv failed, bail out */
				strcpy (recv_buffer + recv_length, "");
				result = STATE_WARNING;
				break;
			}
			else if (recv_result == 0) {
				/* end of file ? */
				recv_buffer[recv_length] = 0;
				break;
			}
			else {										/* we got data! */
				recv_length += recv_result;
				if (recv_length >= recv_size - 1) {
					/* buffer full, we're done */
					recv_buffer[recv_size - 1] = 0;
					break;
				}
			}
		}
		/* end if(!FD_ISSET(sd,&readfds)) */
	}
	/* end while(1) */

	close (sd);
	return result;
}

/* connects to a host on a specified port, sends a string, and gets a 
   response */
int
process_request (const char *server_address, int server_port, int proto,
	const char *send_buffer, char *recv_buffer, int recv_size)
{
	int result;
	int sd;

	result = STATE_OK;

	result = my_connect (server_address, server_port, &sd, proto);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	result = send_request (sd, proto, send_buffer, recv_buffer, recv_size);

	close (sd);

	return result;
}


/* opens a connection to a remote host/tcp port */
int
my_tcp_connect (const char *host_name, int port, int *sd)
{
	int result;

	result = my_connect (host_name, port, sd, IPPROTO_TCP);

	return result;
}


/* opens a connection to a remote host/udp port */
int
my_udp_connect (const char *host_name, int port, int *sd)
{
	int result;

	result = my_connect (host_name, port, sd, IPPROTO_UDP);

	return result;
}


/* opens a tcp or udp connection to a remote host */
static int
my_connect (const char *host_name, int port, int *sd, int proto)
{
	struct addrinfo hints;
	struct addrinfo *res, *res0;
	char port_str[6];
	int result;

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = address_family;
	hints.ai_protocol = proto;
	hints.ai_socktype = (proto == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;

	snprintf (port_str, sizeof (port_str), "%d", port);
	result = getaddrinfo (host_name, port_str, &hints, &res0);

	if (result != 0) {
		printf ("%s\n", gai_strerror (result));
		return STATE_UNKNOWN;
	}
	else {
		res = res0;
		while (res) {
			/* attempt to create a socket */
			*sd = socket (res->ai_family, (proto == IPPROTO_UDP) ?
			              SOCK_DGRAM : SOCK_STREAM, res->ai_protocol);

			if (*sd < 0) {
				printf (_("Socket creation failed\n"));
				freeaddrinfo (res);
				return STATE_UNKNOWN;
			}

			/* attempt to open a connection */
			result = connect (*sd, res->ai_addr, res->ai_addrlen);

			if (result == 0) {
				was_refused = FALSE;
				break;
			}

			if (result < 0) {
				switch (errno) {
				case ECONNREFUSED:
					was_refused = TRUE;
					break;
				}
			}

			close (*sd);
			res = res->ai_next;
		}
		freeaddrinfo (res0);
	}

	if (result == 0)
		return STATE_OK;
	else if (was_refused) {
		switch (econn_refuse_state) { /* a user-defined expected outcome */
		case STATE_OK:       
		case STATE_WARNING:  /* user wants WARN or OK on refusal */
			return econn_refuse_state;
			break;
		case STATE_CRITICAL: /* user did not set econn_refuse_state */
			printf ("%s\n", strerror(errno));
			return econn_refuse_state;
			break;
		default: /* it's a logic error if we do not end up in STATE_(OK|WARNING|CRITICAL) */
			return STATE_UNKNOWN;
			break;
		}
	}
	else {
		printf ("%s\n", strerror(errno));
		return STATE_CRITICAL;
	}
}


int
send_tcp_request (int sd, const char *send_buffer, char *recv_buffer, int recv_size)
{
	return send_request (sd, IPPROTO_TCP, send_buffer, recv_buffer, recv_size);
}


int
send_udp_request (int sd, const char *send_buffer, char *recv_buffer, int recv_size)
{
	return send_request (sd, IPPROTO_UDP, send_buffer, recv_buffer, recv_size);
}


int
send_request (int sd, int proto, const char *send_buffer, char *recv_buffer, int recv_size)
{
	int result = STATE_OK;
	int send_result;
	int recv_result;
	struct timeval tv;
	fd_set readfds;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result<0 || (size_t)send_result!=strlen(send_buffer)) {
		printf (_("Send failed\n"));
		result = STATE_WARNING;
	}

	/* wait up to the number of seconds for socket timeout minus one 
	   for data from the host */
	tv.tv_sec = socket_timeout - 1;
	tv.tv_usec = 0;
	FD_ZERO (&readfds);
	FD_SET (sd, &readfds);
	select (sd + 1, &readfds, NULL, NULL, &tv);

	/* make sure some data has arrived */
	if (!FD_ISSET (sd, &readfds)) {
		strcpy (recv_buffer, "");
		printf (_("No data was received from host!\n"));
		result = STATE_WARNING;
	}

	else {
		recv_result = recv (sd, recv_buffer, (size_t)recv_size - 1, 0);
		if (recv_result == -1) {
			strcpy (recv_buffer, "");
			if (proto != IPPROTO_TCP)
				printf (_("Receive failed\n"));
			result = STATE_WARNING;
		}
		else
			recv_buffer[recv_result] = 0;

		/* die returned string */
		recv_buffer[recv_size - 1] = 0;
	}
	return result;
}


int
is_host (const char *address)
{
	if (is_addr (address) || is_hostname (address))
		return (TRUE);

	return (FALSE);
}

int
is_addr (const char *address)
{
#ifdef USE_IPV6
	if (is_inet_addr (address) && address_family != AF_INET6)
#else
	if (is_inet_addr (address))
#endif
		return (TRUE);

#ifdef USE_IPV6
	if (is_inet6_addr (address) && address_family != AF_INET)
		return (TRUE);
#endif

	return (FALSE);
}

int
resolve_host_or_addr (const char *address, int family)
{
	struct addrinfo hints;
	struct addrinfo *res;
	int retval;

	memset (&hints, 0, sizeof (hints));
	hints.ai_family = family;
	retval = getaddrinfo (address, NULL, &hints, &res);

	if (retval != 0)
		return FALSE;
	else {
		freeaddrinfo (res);
		return TRUE;
	}
}

int
is_inet_addr (const char *address)
{
	return resolve_host_or_addr (address, AF_INET);
}

#ifdef USE_IPV6
int
is_inet6_addr (const char *address)
{
	return resolve_host_or_addr (address, AF_INET6);
}
#endif

int
is_hostname (const char *s1)
{
#ifdef USE_IPV6
	return resolve_host_or_addr (s1, address_family);
#else
	return resolve_host_or_addr (s1, AF_INET);
#endif
}

