/*****************************************************************************
* 
* Nagios plugins network utilities
* 
* License: GPL
* Copyright (c) 1999 Ethan Galstad (nagios@nagios.org)
* Copyright (c) 2003-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains commons functions used in many of the plugins.
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

#include "common.h"
#include "netutils.h"

unsigned int socket_timeout = DEFAULT_SOCKET_TIMEOUT;
unsigned int socket_timeout_state = STATE_CRITICAL;

int econn_refuse_state = STATE_CRITICAL;
int was_refused = FALSE;
#if USE_IPV6
int address_family = AF_UNSPEC;
#else
int address_family = AF_INET;
#endif

/* handles socket timeouts */
void
socket_timeout_alarm_handler (int sig)
{
	if (sig == SIGALRM)
		printf (_("%s - Socket timeout after %d seconds\n"), state_text(socket_timeout_state),  socket_timeout);
	else
		printf (_("%s - Abnormal timeout after %d seconds\n"), state_text(socket_timeout_state), socket_timeout);

	exit (socket_timeout_state);
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

	result = np_net_connect (server_address, server_port, &sd, IPPROTO_TCP);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result<0 || (size_t)send_result!=strlen(send_buffer)) {
		printf ("%s\n", _("Send failed"));
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
				printf ("%s\n", _("No data was received from host!"));
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

	result = np_net_connect (server_address, server_port, &sd, proto);
	if (result != STATE_OK)
		return STATE_CRITICAL;

	result = send_request (sd, proto, send_buffer, recv_buffer, recv_size);

	close (sd);

	return result;
}


/* opens a tcp or udp connection to a remote host or local socket */
int
np_net_connect (const char *host_name, int port, int *sd, int proto)
{
	struct addrinfo hints;
	struct addrinfo *r, *res;
	struct sockaddr_un su;
	char port_str[6], host[MAX_HOST_ADDRESS_LENGTH];
	size_t len;
	int socktype, result;

	socktype = (proto == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;

	/* as long as it doesn't start with a '/', it's assumed a host or ip */
	if(host_name[0] != '/'){
		memset (&hints, 0, sizeof (hints));
		hints.ai_family = address_family;
		hints.ai_protocol = proto;
		hints.ai_socktype = socktype;

		len = strlen (host_name);
		/* check for an [IPv6] address (and strip the brackets) */
		if (len >= 2 && host_name[0] == '[' && host_name[len - 1] == ']') {
			host_name++;
			len -= 2;
		}
		if (len >= sizeof(host))
			return STATE_UNKNOWN;
		memcpy (host, host_name, len);
		host[len] = '\0';
		snprintf (port_str, sizeof (port_str), "%d", port);
		result = getaddrinfo (host, port_str, &hints, &res);

		if (result != 0) {
			printf ("%s\n", gai_strerror (result));
			return STATE_UNKNOWN;
		}

		r = res;
		while (r) {
			/* attempt to create a socket */
			*sd = socket (r->ai_family, socktype, r->ai_protocol);

			if (*sd < 0) {
				printf ("%s\n", _("Socket creation failed"));
				freeaddrinfo (r);
				return STATE_UNKNOWN;
			}

			/* attempt to open a connection */
			result = connect (*sd, r->ai_addr, r->ai_addrlen);

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
			r = r->ai_next;
		}
		freeaddrinfo (res);
	}
	/* else the hostname is interpreted as a path to a unix socket */
	else {
		if(strlen(host_name) >= UNIX_PATH_MAX){
			die(STATE_UNKNOWN, _("Supplied path too long unix domain socket"));
		}
		memset(&su, 0, sizeof(su));
		su.sun_family = AF_UNIX;
		strncpy(su.sun_path, host_name, UNIX_PATH_MAX);
		*sd = socket(PF_UNIX, SOCK_STREAM, 0);
		if(*sd < 0){
			die(STATE_UNKNOWN, _("Socket creation failed"));
		}
		result = connect(*sd, (struct sockaddr *)&su, sizeof(su));
		if (result < 0 && errno == ECONNREFUSED)
			was_refused = TRUE;
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
send_request (int sd, int proto, const char *send_buffer, char *recv_buffer, int recv_size)
{
	int result = STATE_OK;
	int send_result;
	int recv_result;
	struct timeval tv;
	fd_set readfds;

	send_result = send (sd, send_buffer, strlen (send_buffer), 0);
	if (send_result<0 || (size_t)send_result!=strlen(send_buffer)) {
		printf ("%s\n", _("Send failed"));
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
		printf ("%s\n", _("No data was received from host!"));
		result = STATE_WARNING;
	}

	else {
		recv_result = recv (sd, recv_buffer, (size_t)recv_size - 1, 0);
		if (recv_result == -1) {
			strcpy (recv_buffer, "");
			if (proto != IPPROTO_TCP)
				printf ("%s\n", _("Receive failed"));
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

void
host_or_die(const char *str)
{
	if(!str || (!is_addr(str) && !is_hostname(str)))
		usage_va(_("Invalid hostname/address - %s"), str);
}

int
is_addr (const char *address)
{
#ifdef USE_IPV6
	if (address_family == AF_INET && is_inet_addr (address))
		return TRUE;
	else if (address_family == AF_INET6 && is_inet6_addr (address))
		return TRUE;
#else
	if (is_inet_addr (address))
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
