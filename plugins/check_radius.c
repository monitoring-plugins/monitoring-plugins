/*****************************************************************************
 *
 * Monitoring check_radius plugin
 *
 * License: GPL
 * Copyright (c) 1999-2024 Monitoring Plugins Development Team
 *
 * Description:
 *
 * This file contains the check_radius plugin
 *
 * Tests to see if a radius server is accepting connections.
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

const char *progname = "check_radius";
const char *copyright = "2000-2024";
const char *email = "devel@monitoring-plugins.org";

#include "common.h"
#include "utils.h"
#include "netutils.h"
#include "states.h"
#include "check_radius.d/config.h"

#if defined(HAVE_LIBRADCLI)
#	include <radcli/radcli.h>
#elif defined(HAVE_LIBFREERADIUS_CLIENT)
#	include <freeradius-client.h>
#elif defined(HAVE_LIBRADIUSCLIENT_NG)
#	include <radiusclient-ng.h>
#else
#	include <radiusclient.h>
#endif

typedef struct {
	int errorcode;
	check_radius_config config;
} check_radius_config_wrapper;
static check_radius_config_wrapper process_arguments(int /*argc*/, char ** /*argv*/);
static void print_help(void);
void print_usage(void);

#if defined(HAVE_LIBFREERADIUS_CLIENT) || defined(HAVE_LIBRADIUSCLIENT_NG) ||                      \
	defined(HAVE_LIBRADCLI)
#	define my_rc_conf_str(a) rc_conf_str(rch, a)
#	if defined(HAVE_LIBRADCLI)
#		define my_rc_send_server(a, b) rc_send_server(rch, a, b, AUTH)
#	else
#		define my_rc_send_server(a, b) rc_send_server(rch, a, b)
#	endif
#	if defined(HAVE_LIBFREERADIUS_CLIENT) || defined(HAVE_LIBRADCLI)
#		define my_rc_buildreq(a, b, c, d, e, f) rc_buildreq(rch, a, b, c, d, (a)->secret, e, f)
#	else
#		define my_rc_buildreq(a, b, c, d, e, f) rc_buildreq(rch, a, b, c, d, e, f)
#	endif
#	define my_rc_avpair_add(a, b, c, d) rc_avpair_add(rch, a, b, c, -1, d)
#	define my_rc_read_dictionary(a)     rc_read_dictionary(rch, a)
#else
#	define my_rc_conf_str(a)                rc_conf_str(a)
#	define my_rc_send_server(a, b)          rc_send_server(a, b)
#	define my_rc_buildreq(a, b, c, d, e, f) rc_buildreq(a, b, c, d, e, f)
#	define my_rc_avpair_add(a, b, c, d)     rc_avpair_add(a, b, c, d)
#	define my_rc_read_dictionary(a)         rc_read_dictionary(a)
#endif

/* REJECT_RC is only defined in some version of radiusclient. It has
 * been reported from radiusclient-ng 0.5.6 on FreeBSD 7.2-RELEASE */
#ifndef REJECT_RC
#	define REJECT_RC BADRESP_RC
#endif

static int my_rc_read_config(char * /*a*/, rc_handle ** /*rch*/);

static bool verbose = false;

/******************************************************************************

The (pseudo?)literate programming XML is contained within \@\@\- <XML> \-\@\@
tags in the comments. With in the tags, the XML is assembled sequentially.
You can define entities in tags. You also have all the #defines available as
entities.

Please note that all tags must be lowercase to use the DocBook XML DTD.

@@-<article>

<sect1>
<title>Quick Reference</title>
<!-- The refentry forms a manpage -->
<refentry>
<refmeta>
<manvolnum>5<manvolnum>
</refmeta>
<refnamdiv>
<refname>&progname;</refname>
<refpurpose>&SUMMARY;</refpurpose>
</refnamdiv>
</refentry>
</sect1>

<sect1>
<title>FAQ</title>
</sect1>

<sect1>
<title>Theory, Installation, and Operation</title>

<sect2>
<title>General Description</title>
<para>
&DESCRIPTION;
</para>
</sect2>

<sect2>
<title>Future Enhancements</title>
<para>Todo List</para>
<itemizedlist>
<listitem>Add option to get password from a secured file rather than the command line</listitem>
</itemizedlist>
</sect2>


<sect2>
<title>Functions</title>
-@@
******************************************************************************/

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_radius_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode == ERROR) {
		usage4(_("Could not parse arguments"));
	}

	check_radius_config config = tmp_config.config;

#if defined(HAVE_LIBFREERADIUS_CLIENT) || defined(HAVE_LIBRADIUSCLIENT_NG) ||                      \
	defined(HAVE_LIBRADCLI)
	rc_handle *rch = NULL;
#endif

	char *str = strdup("dictionary");
	if ((config.config_file && my_rc_read_config(config.config_file, &rch)) ||
		my_rc_read_dictionary(my_rc_conf_str(str))) {
		die(STATE_UNKNOWN, _("Config file error\n"));
	}

	uint32_t service = PW_AUTHENTICATE_ONLY;

	SEND_DATA data;
	memset(&data, 0, sizeof(data));
	if (!(my_rc_avpair_add(&data.send_pairs, PW_SERVICE_TYPE, &service, 0) &&
		  my_rc_avpair_add(&data.send_pairs, PW_USER_NAME, config.username, 0) &&
		  my_rc_avpair_add(&data.send_pairs, PW_USER_PASSWORD, config.password, 0))) {
		die(STATE_UNKNOWN, _("Out of Memory?\n"));
	}

	if (config.nas_id != NULL) {
		if (!(my_rc_avpair_add(&data.send_pairs, PW_NAS_IDENTIFIER, config.nas_id, 0))) {
			die(STATE_UNKNOWN, _("Invalid NAS-Identifier\n"));
		}
	}

	char name[HOST_NAME_MAX];
	if (config.nas_ip_address == NULL) {
		if (gethostname(name, sizeof(name)) != 0) {
			die(STATE_UNKNOWN, _("gethostname() failed!\n"));
		}
		config.nas_ip_address = name;
	}

	struct sockaddr_storage radius_server_socket;
	if (!dns_lookup(config.nas_ip_address, &radius_server_socket, AF_UNSPEC)) {
		die(STATE_UNKNOWN, _("Invalid NAS-IP-Address\n"));
	}

	uint32_t client_id = ntohl(((struct sockaddr_in *)&radius_server_socket)->sin_addr.s_addr);
	if (my_rc_avpair_add(&(data.send_pairs), PW_NAS_IP_ADDRESS, &client_id, 0) == NULL) {
		die(STATE_UNKNOWN, _("Invalid NAS-IP-Address\n"));
	}

	my_rc_buildreq(&data, PW_ACCESS_REQUEST, config.server, config.port, (int)timeout_interval,
				   config.retries);

#ifdef RC_BUFFER_LEN
	char msg[RC_BUFFER_LEN];
#else
	char msg[BUFFER_LEN];
#endif

	int result = my_rc_send_server(&data, msg);
	rc_avpair_free(data.send_pairs);
	if (data.receive_pairs) {
		rc_avpair_free(data.receive_pairs);
	}

	if (result == TIMEOUT_RC) {
		printf("Timeout\n");
		exit(STATE_CRITICAL);
	}

	if (result == ERROR_RC) {
		printf(_("Auth Error\n"));
		exit(STATE_CRITICAL);
	}

	if (result == REJECT_RC) {
		printf(_("Auth Failed\n"));
		exit(STATE_WARNING);
	}

	if (result == BADRESP_RC) {
		printf(_("Bad Response\n"));
		exit(STATE_WARNING);
	}

	if (config.expect && !strstr(msg, config.expect)) {
		printf("%s\n", msg);
		exit(STATE_WARNING);
	}

	if (result == OK_RC) {
		printf(_("Auth OK\n"));
		exit(STATE_OK);
	}

	(void)snprintf(msg, sizeof(msg), _("Unexpected result code %d"), result);
	printf("%s\n", msg);
	exit(STATE_UNKNOWN);
}

/* process command-line arguments */
check_radius_config_wrapper process_arguments(int argc, char **argv) {
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'}, {"port", required_argument, 0, 'P'},
		{"username", required_argument, 0, 'u'}, {"password", required_argument, 0, 'p'},
		{"nas-id", required_argument, 0, 'n'},   {"nas-ip-address", required_argument, 0, 'N'},
		{"filename", required_argument, 0, 'F'}, {"expect", required_argument, 0, 'e'},
		{"retries", required_argument, 0, 'r'},  {"timeout", required_argument, 0, 't'},
		{"verbose", no_argument, 0, 'v'},        {"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},           {0, 0, 0, 0}};

	check_radius_config_wrapper result = {
		.errorcode = OK,
		.config = check_radius_config_init(),
	};

	while (true) {
		int option = 0;
		int option_index = getopt_long(argc, argv, "+hVvH:P:F:u:p:n:N:t:r:e:", longopts, &option);

		if (option_index == -1 || option_index == EOF || option_index == 1) {
			break;
		}

		switch (option_index) {
		case '?': /* print short usage statement if args not parsable */
			usage5();
		case 'h': /* help */
			print_help();
			exit(STATE_UNKNOWN);
		case 'V': /* version */
			print_revision(progname, NP_VERSION);
			exit(STATE_UNKNOWN);
		case 'v': /* verbose mode */
			verbose = true;
			break;
		case 'H': /* hostname */
			if (!is_host(optarg)) {
				usage2(_("Invalid hostname/address"), optarg);
			}
			result.config.server = optarg;
			break;
		case 'P': /* port */
			if (is_intnonneg(optarg)) {
				result.config.port = (unsigned short)atoi(optarg);
			} else {
				usage4(_("Port must be a positive integer"));
			}
			break;
		case 'u': /* username */
			result.config.username = optarg;
			break;
		case 'p': /* password */
			result.config.password = strdup(optarg);

			/* Delete the password from process list */
			while (*optarg != '\0') {
				*optarg = 'X';
				optarg++;
			}
			break;
		case 'n': /* nas id */
			result.config.nas_id = optarg;
			break;
		case 'N': /* nas ip address */
			result.config.nas_ip_address = optarg;
			break;
		case 'F': /* configuration file */
			result.config.config_file = optarg;
			break;
		case 'e': /* expect */
			result.config.expect = optarg;
			break;
		case 'r': /* retries */
			if (is_intpos(optarg)) {
				result.config.retries = atoi(optarg);
			} else {
				usage4(_("Number of retries must be a positive integer"));
			}
			break;
		case 't': /* timeout */
			if (is_intpos(optarg)) {
				timeout_interval = (unsigned)atoi(optarg);
			} else {
				usage2(_("Timeout interval must be a positive integer"), optarg);
			}
			break;
		}
	}

	if (result.config.server == NULL) {
		usage4(_("Hostname was not supplied"));
	}
	if (result.config.username == NULL) {
		usage4(_("User not specified"));
	}
	if (result.config.password == NULL) {
		usage4(_("Password not specified"));
	}
	if (result.config.config_file == NULL) {
		usage4(_("Configuration file not specified"));
	}

	return result;
}

void print_help(void) {
	char *myport;
	xasprintf(&myport, "%d", PW_AUTH_UDP_PORT);

	print_revision(progname, NP_VERSION);

	printf("Copyright (c) 1999 Robert August Vincent II\n");
	printf(COPYRIGHT, copyright, email);

	printf("%s\n", _("Tests to see if a RADIUS server is accepting connections."));

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(UT_HOST_PORT, 'P', myport);

	printf(" %s\n", "-u, --username=STRING");
	printf("    %s\n", _("The user to authenticate"));
	printf(" %s\n", "-p, --password=STRING");
	printf("    %s\n", _("Password for authentication (SECURITY RISK)"));
	printf(" %s\n", "-n, --nas-id=STRING");
	printf("    %s\n", _("NAS identifier"));
	printf(" %s\n", "-N, --nas-ip-address=STRING");
	printf("    %s\n", _("NAS IP Address"));
	printf(" %s\n", "-F, --filename=STRING");
	printf("    %s\n", _("Configuration file"));
	printf(" %s\n", "-e, --expect=STRING");
	printf("    %s\n", _("Response string to expect from the server"));
	printf(" %s\n", "-r, --retries=INTEGER");
	printf("    %s\n", _("Number of times to retry a failed connection"));

	printf(UT_CONN_TIMEOUT, timeout_interval);

	printf("\n");
	printf("%s\n", _("This plugin tests a RADIUS server to see if it is accepting connections."));
	printf("%s\n", _("The server to test must be specified in the invocation, as well as a user"));
	printf("%s\n", _("name and password. A configuration file must be present. The format of"));
	printf("%s\n", _("the configuration file is described in the radiusclient library sources."));
	printf("%s\n", _("The password option presents a substantial security issue because the"));
	printf("%s\n",
		   _("password can possibly be determined by careful watching of the command line"));
	printf("%s\n", _("in a process listing. This risk is exacerbated because the plugin will"));
	printf("%s\n",
		   _("typically be executed at regular predictable intervals. Please be sure that"));
	printf("%s\n", _("the password used does not allow access to sensitive system resources."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf("%s -H host -F config_file -u username -p password\n\
			[-P port] [-t timeout] [-r retries] [-e expect]\n\
			[-n nas-id] [-N nas-ip-addr]\n",
		   progname);
}

int my_rc_read_config(char *config_file_name, rc_handle **rch) {
#if defined(HAVE_LIBFREERADIUS_CLIENT) || defined(HAVE_LIBRADIUSCLIENT_NG) ||                      \
	defined(HAVE_LIBRADCLI)
	*rch = rc_read_config(config_file_name);
	return (rch == NULL) ? 1 : 0;
#else
	return rc_read_config(config_file_name);
#endif
}
