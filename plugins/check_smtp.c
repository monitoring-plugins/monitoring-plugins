/*****************************************************************************
* 
* Nagios check_smtp plugin
* 
* License: GPL
* Copyright (c) 2000-2007 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_smtp plugin
* 
* This plugin will attempt to open an SMTP connection with the host.
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

const char *progname = "check_smtp";
const char *copyright = "2000-2007";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"
#include "base64.h"

#include <ctype.h>

#ifdef HAVE_SSL
int check_cert = FALSE;
int days_till_exp_warn, days_till_exp_crit;
#  define my_recv(buf, len) ((use_ssl && ssl_established) ? np_net_ssl_read(buf, len) : read(sd, buf, len))
#  define my_send(buf, len) ((use_ssl && ssl_established) ? np_net_ssl_write(buf, len) : send(sd, buf, len, 0))
#else /* ifndef HAVE_SSL */
#  define my_recv(buf, len) read(sd, buf, len)
#  define my_send(buf, len) send(sd, buf, len, 0)
#endif

enum {
	SMTP_PORT	= 25
};
#define SMTP_EXPECT "220"
#define SMTP_HELO "HELO "
#define SMTP_EHLO "EHLO "
#define SMTP_QUIT "QUIT\r\n"
#define SMTP_STARTTLS "STARTTLS\r\n"
#define SMTP_AUTH_LOGIN "AUTH LOGIN\r\n"

#ifndef HOST_MAX_BYTES
#define HOST_MAX_BYTES 255
#endif

#define EHLO_SUPPORTS_STARTTLS 1

int process_arguments (int, char **);
int validate_arguments (void);
void print_help (void);
void print_usage (void);
void smtp_quit(void);
int recvline(char *, size_t);
int recvlines(char *, size_t);
int my_close(void);

#include "regex.h"
char regex_expect[MAX_INPUT_BUFFER] = "";
regex_t preg;
regmatch_t pmatch[10];
char timestamp[20] = "";
char errbuf[MAX_INPUT_BUFFER];
int cflags = REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
int eflags = 0;
int errcode, excode;

int server_port = SMTP_PORT;
char *server_address = NULL;
char *server_expect = NULL;
char *mail_command = NULL;
char *from_arg = NULL;
int send_mail_from=0;
int ncommands=0;
int command_size=0;
int nresponses=0;
int response_size=0;
char **commands = NULL;
char **responses = NULL;
char *authtype = NULL;
char *authuser = NULL;
char *authpass = NULL;
double warning_time = 0;
int check_warning_time = FALSE;
double critical_time = 0;
int check_critical_time = FALSE;
int verbose = 0;
int use_ssl = FALSE;
short use_ehlo = FALSE;
short ssl_established = 0;
char *localhostname = NULL;
int sd;
char buffer[MAX_INPUT_BUFFER];
enum {
  TCP_PROTOCOL = 1,
  UDP_PROTOCOL = 2,
};
int ignore_send_quit_failure = FALSE;


int
main (int argc, char **argv)
{
	short supports_tls=FALSE;
	int n = 0;
	double elapsed_time;
	long microsec;
	int result = STATE_UNKNOWN;
	char *cmd_str = NULL;
	char *helocmd = NULL;
	char *error_msg = "";
	struct timeval tv;

	/* Catch pipe errors in read/write - sometimes occurs when writing QUIT */
	(void) signal (SIGPIPE, SIG_IGN);

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* If localhostname not set on command line, use gethostname to set */
	if(! localhostname){
		localhostname = malloc (HOST_MAX_BYTES);
		if(!localhostname){
			printf(_("malloc() failed!\n"));
			return STATE_CRITICAL;
		}
		if(gethostname(localhostname, HOST_MAX_BYTES)){
			printf(_("gethostname() failed!\n"));
			return STATE_CRITICAL;
		}
	}
	if(use_ehlo)
		xasprintf (&helocmd, "%s%s%s", SMTP_EHLO, localhostname, "\r\n");
	else
		xasprintf (&helocmd, "%s%s%s", SMTP_HELO, localhostname, "\r\n");

	if (verbose)
		printf("HELOCMD: %s", helocmd);

	/* initialize the MAIL command with optional FROM command  */
	xasprintf (&cmd_str, "%sFROM:<%s>%s", mail_command, from_arg, "\r\n");

	if (verbose && send_mail_from)
		printf ("FROM CMD: %s", cmd_str);

	/* initialize alarm signal handling */
	(void) signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	(void) alarm (socket_timeout);

	/* start timer */
	gettimeofday (&tv, NULL);

	/* try to connect to the host at the given port number */
	result = my_tcp_connect (server_address, server_port, &sd);

	if (result == STATE_OK) { /* we connected */

		/* watch for the SMTP connection string and */
		/* return a WARNING status if we couldn't read any data */
		if (recvlines(buffer, MAX_INPUT_BUFFER) <= 0) {
			printf (_("recv() failed\n"));
			return STATE_WARNING;
		}
		else {
			if (verbose)
				printf ("%s", buffer);
			/* strip the buffer of carriage returns */
			strip (buffer);
			/* make sure we find the response we are looking for */
			if (!strstr (buffer, server_expect)) {
				if (server_port == SMTP_PORT)
					printf (_("Invalid SMTP response received from host: %s\n"), buffer);
				else
					printf (_("Invalid SMTP response received from host on port %d: %s\n"),
									server_port, buffer);
				return STATE_WARNING;
			}
		}

		/* send the HELO/EHLO command */
		send(sd, helocmd, strlen(helocmd), 0);

		/* allow for response to helo command to reach us */
		if (recvlines(buffer, MAX_INPUT_BUFFER) <= 0) {
			printf (_("recv() failed\n"));
			return STATE_WARNING;
		} else if(use_ehlo){
			if(strstr(buffer, "250 STARTTLS") != NULL ||
			   strstr(buffer, "250-STARTTLS") != NULL){
				supports_tls=TRUE;
			}
		}

		if(use_ssl && ! supports_tls){
			printf(_("WARNING - TLS not supported by server\n"));
			smtp_quit();
			return STATE_WARNING;
		}

#ifdef HAVE_SSL
		if(use_ssl) {
		  /* send the STARTTLS command */
		  send(sd, SMTP_STARTTLS, strlen(SMTP_STARTTLS), 0);

		  recvlines(buffer, MAX_INPUT_BUFFER); /* wait for it */
		  if (!strstr (buffer, server_expect)) {
		    printf (_("Server does not support STARTTLS\n"));
		    smtp_quit();
		    return STATE_UNKNOWN;
		  }
		  result = np_net_ssl_init(sd);
		  if(result != STATE_OK) {
		    printf (_("CRITICAL - Cannot create SSL context.\n"));
		    np_net_ssl_cleanup();
		    close(sd);
		    return STATE_CRITICAL;
		  } else {
			ssl_established = 1;
		  }

		/*
		 * Resend the EHLO command.
		 *
		 * RFC 3207 (4.2) says: ``The client MUST discard any knowledge
		 * obtained from the server, such as the list of SMTP service
		 * extensions, which was not obtained from the TLS negotiation
		 * itself.  The client SHOULD send an EHLO command as the first
		 * command after a successful TLS negotiation.''  For this
		 * reason, some MTAs will not allow an AUTH LOGIN command before
		 * we resent EHLO via TLS.
		 */
		if (my_send(helocmd, strlen(helocmd)) <= 0) {
			printf("%s\n", _("SMTP UNKNOWN - Cannot send EHLO command via TLS."));
			my_close();
			return STATE_UNKNOWN;
		}
		if (verbose)
			printf(_("sent %s"), helocmd);
		if ((n = recvlines(buffer, MAX_INPUT_BUFFER)) <= 0) {
			printf("%s\n", _("SMTP UNKNOWN - Cannot read EHLO response via TLS."));
			my_close();
			return STATE_UNKNOWN;
		}
		if (verbose) {
			printf("%s", buffer);
		}

#  ifdef USE_OPENSSL
		  if ( check_cert ) {
                    result = np_net_ssl_check_cert(days_till_exp_warn, days_till_exp_crit);
		    my_close();
		    return result;
		  }
#  endif /* USE_OPENSSL */
		}
#endif

		if (send_mail_from) {
		  my_send(cmd_str, strlen(cmd_str));
		  if (recvlines(buffer, MAX_INPUT_BUFFER) >= 1 && verbose)
		    printf("%s", buffer);
		}

		while (n < ncommands) {
			xasprintf (&cmd_str, "%s%s", commands[n], "\r\n");
			my_send(cmd_str, strlen(cmd_str));
			if (recvlines(buffer, MAX_INPUT_BUFFER) >= 1 && verbose)
				printf("%s", buffer);
			strip (buffer);
			if (n < nresponses) {
				cflags |= REG_EXTENDED | REG_NOSUB | REG_NEWLINE;
				errcode = regcomp (&preg, responses[n], cflags);
				if (errcode != 0) {
					regerror (errcode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Could Not Compile Regular Expression"));
					return ERROR;
				}
				excode = regexec (&preg, buffer, 10, pmatch, eflags);
				if (excode == 0) {
					result = STATE_OK;
				}
				else if (excode == REG_NOMATCH) {
					result = STATE_WARNING;
					printf (_("SMTP %s - Invalid response '%s' to command '%s'\n"), state_text (result), buffer, commands[n]);
				}
				else {
					regerror (excode, &preg, errbuf, MAX_INPUT_BUFFER);
					printf (_("Execute Error: %s\n"), errbuf);
					result = STATE_UNKNOWN;
				}
			}
			n++;
		}

		if (authtype != NULL) {
			if (strcmp (authtype, "LOGIN") == 0) {
				char *abuf;
				int ret;
				do {
					if (authuser == NULL) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("no authuser specified, "));
						break;
					}
					if (authpass == NULL) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("no authpass specified, "));
						break;
					}

					/* send AUTH LOGIN */
					my_send(SMTP_AUTH_LOGIN, strlen(SMTP_AUTH_LOGIN));
					if (verbose)
						printf (_("sent %s\n"), "AUTH LOGIN");

					if ((ret = recvlines(buffer, MAX_INPUT_BUFFER)) <= 0) {
						xasprintf(&error_msg, _("recv() failed after AUTH LOGIN, "));
						result = STATE_WARNING;
						break;
					}
					if (verbose)
						printf (_("received %s\n"), buffer);

					if (strncmp (buffer, "334", 3) != 0) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("invalid response received after AUTH LOGIN, "));
						break;
					}

					/* encode authuser with base64 */
					base64_encode_alloc (authuser, strlen(authuser), &abuf);
					xasprintf(&abuf, "%s\r\n", abuf);
					my_send(abuf, strlen(abuf));
					if (verbose)
						printf (_("sent %s\n"), abuf);

					if ((ret = recvlines(buffer, MAX_INPUT_BUFFER)) <= 0) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("recv() failed after sending authuser, "));
						break;
					}
					if (verbose) {
						printf (_("received %s\n"), buffer);
					}
					if (strncmp (buffer, "334", 3) != 0) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("invalid response received after authuser, "));
						break;
					}
					/* encode authpass with base64 */
					base64_encode_alloc (authpass, strlen(authpass), &abuf);
					xasprintf(&abuf, "%s\r\n", abuf);
					my_send(abuf, strlen(abuf));
					if (verbose) {
						printf (_("sent %s\n"), abuf);
					}
					if ((ret = recvlines(buffer, MAX_INPUT_BUFFER)) <= 0) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("recv() failed after sending authpass, "));
						break;
					}
					if (verbose) {
						printf (_("received %s\n"), buffer);
					}
					if (strncmp (buffer, "235", 3) != 0) {
						result = STATE_CRITICAL;
						xasprintf(&error_msg, _("invalid response received after authpass, "));
						break;
					}
					break;
				} while (0);
			} else {
				result = STATE_CRITICAL;
				xasprintf(&error_msg, _("only authtype LOGIN is supported, "));
			}
		}

		/* tell the server we're done */
		smtp_quit();

		/* finally close the connection */
		close (sd);
	}

	/* reset the alarm */
	alarm (0);

	microsec = deltime (tv);
	elapsed_time = (double)microsec / 1.0e6;

	if (result == STATE_OK) {
		if (check_critical_time && elapsed_time > critical_time)
			result = STATE_CRITICAL;
		else if (check_warning_time && elapsed_time > warning_time)
			result = STATE_WARNING;
	}

	printf (_("SMTP %s - %s%.3f sec. response time%s%s|%s\n"),
			state_text (result),
			error_msg,
			elapsed_time,
			verbose?", ":"", verbose?buffer:"",
			fperfdata ("time", elapsed_time, "s",
				(int)check_warning_time, warning_time,
				(int)check_critical_time, critical_time,
				TRUE, 0, FALSE, 0));

	return result;
}



/* process command-line arguments */
int
process_arguments (int argc, char **argv)
{
	int c;
	char* temp;

	int option = 0;
	static struct option longopts[] = {
		{"hostname", required_argument, 0, 'H'},
		{"expect", required_argument, 0, 'e'},
		{"critical", required_argument, 0, 'c'},
		{"warning", required_argument, 0, 'w'},
		{"timeout", required_argument, 0, 't'},
		{"port", required_argument, 0, 'p'},
		{"from", required_argument, 0, 'f'},
		{"fqdn", required_argument, 0, 'F'},
		{"authtype", required_argument, 0, 'A'},
		{"authuser", required_argument, 0, 'U'},
		{"authpass", required_argument, 0, 'P'},
		{"command", required_argument, 0, 'C'},
		{"response", required_argument, 0, 'R'},
		{"verbose", no_argument, 0, 'v'},
		{"version", no_argument, 0, 'V'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"help", no_argument, 0, 'h'},
		{"starttls",no_argument,0,'S'},
		{"certificate",required_argument,0,'D'},
		{"ignore-quit-failure",no_argument,0,'q'},
		{0, 0, 0, 0}
	};

	if (argc < 2)
		return ERROR;

	for (c = 1; c < argc; c++) {
		if (strcmp ("-to", argv[c]) == 0)
			strcpy (argv[c], "-t");
		else if (strcmp ("-wt", argv[c]) == 0)
			strcpy (argv[c], "-w");
		else if (strcmp ("-ct", argv[c]) == 0)
			strcpy (argv[c], "-c");
	}

	while (1) {
		c = getopt_long (argc, argv, "+hVv46t:p:f:e:c:w:H:C:R:SD:F:A:U:P:q",
		                 longopts, &option);

		if (c == -1 || c == EOF)
			break;

		switch (c) {
		case 'H':									/* hostname */
			if (is_host (optarg)) {
				server_address = optarg;
			}
			else {
				usage2 (_("Invalid hostname/address"), optarg);
			}
			break;
		case 'p':									/* port */
			if (is_intpos (optarg))
				server_port = atoi (optarg);
			else
				usage4 (_("Port must be a positive integer"));
			break;
		case 'F':
		/* localhostname */
			localhostname = strdup(optarg);
			break;
		case 'f':									/* from argument */
			from_arg = optarg + strspn(optarg, "<");
			from_arg = strndup(from_arg, strcspn(from_arg, ">"));
			send_mail_from = 1;
			break;
		case 'A':
			authtype = optarg;
			use_ehlo = TRUE;
			break;
		case 'U':
			authuser = optarg;
			break;
		case 'P':
			authpass = optarg;
			break;
		case 'e':									/* server expect string on 220  */
			server_expect = optarg;
			break;
		case 'C':									/* commands  */
			if (ncommands >= command_size) {
				command_size+=8;
				commands = realloc (commands, sizeof(char *) * command_size);
				if (commands == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), ncommands);
			}
			commands[ncommands] = (char *) malloc (sizeof(char) * 255);
			strncpy (commands[ncommands], optarg, 255);
			ncommands++;
			break;
		case 'R':									/* server responses */
			if (nresponses >= response_size) {
				response_size += 8;
				responses = realloc (responses, sizeof(char *) * response_size);
				if (responses == NULL)
					die (STATE_UNKNOWN,
					     _("Could not realloc() units [%d]\n"), nresponses);
			}
			responses[nresponses] = (char *) malloc (sizeof(char) * 255);
			strncpy (responses[nresponses], optarg, 255);
			nresponses++;
			break;
		case 'c':									/* critical time threshold */
			if (!is_nonnegative (optarg))
				usage4 (_("Critical time must be a positive"));
			else {
				critical_time = strtod (optarg, NULL);
				check_critical_time = TRUE;
			}
			break;
		case 'w':									/* warning time threshold */
			if (!is_nonnegative (optarg))
				usage4 (_("Warning time must be a positive"));
			else {
				warning_time = strtod (optarg, NULL);
				check_warning_time = TRUE;
			}
			break;
		case 'v':									/* verbose */
			verbose++;
			break;
		case 'q':
			ignore_send_quit_failure++;             /* ignore problem sending QUIT */
			break;
		case 't':									/* timeout */
			if (is_intnonneg (optarg)) {
				socket_timeout = atoi (optarg);
			}
			else {
				usage4 (_("Timeout interval must be a positive integer"));
			}
			break;
		case 'S':
		/* starttls */
			use_ssl = TRUE;
			use_ehlo = TRUE;
			break;
		case 'D':
		/* Check SSL cert validity */
#ifdef USE_OPENSSL
                        if ((temp=strchr(optarg,','))!=NULL) {
                            *temp='\0';
                            if (!is_intnonneg (optarg))
                               usage2 ("Invalid certificate expiration period", optarg);
                            days_till_exp_warn = atoi(optarg);
                            *temp=',';
                            temp++;
                            if (!is_intnonneg (temp))
                                usage2 (_("Invalid certificate expiration period"), temp);
                            days_till_exp_crit = atoi (temp);
                        }
                        else {
                            days_till_exp_crit=0;
                            if (!is_intnonneg (optarg))
                                usage2 ("Invalid certificate expiration period", optarg);
                            days_till_exp_warn = atoi (optarg);
                        }
			check_cert = TRUE;
#else
			usage (_("SSL support not available - install OpenSSL and recompile"));
#endif
			break;
		case '4':
			address_family = AF_INET;
			break;
		case '6':
#ifdef USE_IPV6
			address_family = AF_INET6;
#else
			usage4 (_("IPv6 support not available"));
#endif
			break;
		case 'V':									/* version */
			print_revision (progname, NP_VERSION);
			exit (STATE_OK);
		case 'h':									/* help */
			print_help ();
			exit (STATE_OK);
		case '?':									/* help */
			usage5 ();
		}
	}

	c = optind;
	if (server_address == NULL) {
		if (argv[c]) {
			if (is_host (argv[c]))
				server_address = argv[c];
			else
				usage2 (_("Invalid hostname/address"), argv[c]);
		}
		else {
			xasprintf (&server_address, "127.0.0.1");
		}
	}

	if (server_expect == NULL)
		server_expect = strdup (SMTP_EXPECT);

	if (mail_command == NULL)
		mail_command = strdup("MAIL ");

	if (from_arg==NULL)
		from_arg = strdup(" ");

	return validate_arguments ();
}



int
validate_arguments (void)
{
	return OK;
}


void
smtp_quit(void)
{
	int bytes;
	int n;

	n = my_send(SMTP_QUIT, strlen(SMTP_QUIT));
	if(n < 0) {
		if(ignore_send_quit_failure) {
			if(verbose) {
				printf(_("Connection closed by server before sending QUIT command\n"));
			}
			return;
		}
		die (STATE_UNKNOWN,
			_("Connection closed by server before sending QUIT command\n"));
	}

	if (verbose)
		printf(_("sent %s\n"), "QUIT");

	/* read the response but don't care about problems */
	bytes = recvlines(buffer, MAX_INPUT_BUFFER);
	if (verbose) {
		if (bytes < 0)
			printf(_("recv() failed after QUIT."));
		else if (bytes == 0)
			printf(_("Connection reset by peer."));
		else {
			buffer[bytes] = '\0';
			printf(_("received %s\n"), buffer);
		}
	}
}


/*
 * Receive one line, copy it into buf and nul-terminate it.  Returns the
 * number of bytes written to buf (excluding the '\0') or 0 on EOF or <0 on
 * error.
 *
 * TODO: Reading one byte at a time is very inefficient.  Replace this by a
 * function which buffers the data, move that to netutils.c and change
 * check_smtp and other plugins to use that.  Also, remove (\r)\n.
 */
int
recvline(char *buf, size_t bufsize)
{
	int result;
	unsigned i;

	for (i = result = 0; i < bufsize - 1; i++) {
		if ((result = my_recv(&buf[i], 1)) != 1)
			break;
		if (buf[i] == '\n') {
			buf[++i] = '\0';
			return i;
		}
	}
	return (result == 1 || i == 0) ? -2 : result;	/* -2 if out of space */
}


/*
 * Receive one or more lines, copy them into buf and nul-terminate it.  Returns
 * the number of bytes written to buf (excluding the '\0') or 0 on EOF or <0 on
 * error.  Works for all protocols which format multiline replies as follows:
 *
 * ``The format for multiline replies requires that every line, except the last,
 * begin with the reply code, followed immediately by a hyphen, `-' (also known
 * as minus), followed by text.  The last line will begin with the reply code,
 * followed immediately by <SP>, optionally some text, and <CRLF>.  As noted
 * above, servers SHOULD send the <SP> if subsequent text is not sent, but
 * clients MUST be prepared for it to be omitted.'' (RFC 2821, 4.2.1)
 *
 * TODO: Move this to netutils.c.  Also, remove \r and possibly the final \n.
 */
int
recvlines(char *buf, size_t bufsize)
{
	int result, i;

	for (i = 0; /* forever */; i += result)
		if (!((result = recvline(buf + i, bufsize - i)) > 3 &&
		    isdigit((int)buf[i]) &&
		    isdigit((int)buf[i + 1]) &&
		    isdigit((int)buf[i + 2]) &&
		    buf[i + 3] == '-'))
			break;

	return (result <= 0) ? result : result + i;
}


int
my_close (void)
{
#ifdef HAVE_SSL
  np_net_ssl_cleanup();
#endif
  return close(sd);
}


void
print_help (void)
{
	char *myport;
	xasprintf (&myport, "%d", SMTP_PORT);

	print_revision (progname, NP_VERSION);

	printf ("Copyright (c) 1999-2001 Ethan Galstad <nagios@nagios.org>\n");
	printf (COPYRIGHT, copyright, email);

	printf("%s\n", _("This plugin will attempt to open an SMTP connection with the host."));

  printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (UT_HOST_PORT, 'p', myport);

	printf (UT_IPv46);

	printf (" %s\n", "-e, --expect=STRING");
  printf (_("    String to expect in first line of server response (default: '%s')\n"), SMTP_EXPECT);
  printf (" %s\n", "-C, --command=STRING");
  printf ("    %s\n", _("SMTP command (may be used repeatedly)"));
  printf (" %s\n", "-R, --response=STRING");
  printf ("    %s\n", _("Expected response to command (may be used repeatedly)"));
  printf (" %s\n", "-f, --from=STRING");
  printf ("    %s\n", _("FROM-address to include in MAIL command, required by Exchange 2000")),
  printf (" %s\n", "-F, --fqdn=STRING");
  printf ("    %s\n", _("FQDN used for HELO"));
#ifdef HAVE_SSL
  printf (" %s\n", "-D, --certificate=INTEGER[,INTEGER]");
  printf ("    %s\n", _("Minimum number of days a certificate has to be valid."));
  printf (" %s\n", "-S, --starttls");
  printf ("    %s\n", _("Use STARTTLS for the connection."));
#endif

	printf (" %s\n", "-A, --authtype=STRING");
  printf ("    %s\n", _("SMTP AUTH type to check (default none, only LOGIN supported)"));
  printf (" %s\n", "-U, --authuser=STRING");
  printf ("    %s\n", _("SMTP AUTH username"));
  printf (" %s\n", "-P, --authpass=STRING");
  printf ("    %s\n", _("SMTP AUTH password"));
  printf (" %s\n", "-q, --ignore-quit-failure");
  printf ("    %s\n", _("Ignore failure when sending QUIT command to server"));
   
	printf (UT_WARN_CRIT);

	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);

	printf (UT_VERBOSE);

	printf("\n");
	printf ("%s\n", _("Successul connects return STATE_OK, refusals and timeouts return"));
  printf ("%s\n", _("STATE_CRITICAL, other errors return STATE_UNKNOWN.  Successful"));
  printf ("%s\n", _("connects, but incorrect reponse messages from the host result in"));
  printf ("%s\n", _("STATE_WARNING return values."));

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
  printf ("%s\n", _("Usage:"));
  printf ("%s -H host [-p port] [-4|-6] [-e expect] [-C command] [-R response] [-f from addr]\n", progname);
  printf ("[-A authtype -U authuser -P authpass] [-w warn] [-c crit] [-t timeout] [-q]\n");
  printf ("[-F fqdn] [-S] [-D warn days cert expire[,crit days cert expire]] [-v] \n");
}

