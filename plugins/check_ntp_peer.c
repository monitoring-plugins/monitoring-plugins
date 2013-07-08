/*****************************************************************************
* 
* Nagios check_ntp_peer plugin
* 
* License: GPL
* Copyright (c) 2006 Sean Finney <seanius@seanius.net>
* Copyright (c) 2006-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_ntp_peer plugin
* 
* This plugin checks an NTP server independent of any commandline
* programs or external libraries.
* 
* Use this plugin to check the health of an NTP server. It supports
* checking the offset with the sync peer, the jitter and stratum. This
* plugin will not check the clock offset between the local host and NTP
* server; please use check_ntp_time for that purpose.
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

const char *progname = "check_ntp_peer";
const char *copyright = "2006-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

static char *server_address=NULL;
static int port=123;
static int verbose=0;
static int quiet=0;
static short do_offset=0;
static char *owarn="60";
static char *ocrit="120";
static short do_stratum=0;
static char *swarn="-1:16";
static char *scrit="-1:16";
static short do_jitter=0;
static char *jwarn="-1:5000";
static char *jcrit="-1:10000";
static short do_truechimers=0;
static char *twarn="0:";
static char *tcrit="0:";
static int syncsource_found=0;
static int li_alarm=0;

int process_arguments (int, char **);
thresholds *offset_thresholds = NULL;
thresholds *jitter_thresholds = NULL;
thresholds *stratum_thresholds = NULL;
thresholds *truechimer_thresholds = NULL;
void print_help (void);
void print_usage (void);

/* max size of control message data */
#define MAX_CM_SIZE 468

/* this structure holds everything in an ntp control message as per rfc1305 */
typedef struct {
	uint8_t flags;       /* byte with leapindicator,vers,mode. see macros */
	uint8_t op;          /* R,E,M bits and Opcode */
	uint16_t seq;        /* Packet sequence */
	uint16_t status;     /* Clock status */
	uint16_t assoc;      /* Association */
	uint16_t offset;     /* Similar to TCP sequence # */
	uint16_t count;      /* # bytes of data */
	char data[MAX_CM_SIZE]; /* ASCII data of the request */
	                        /* NB: not necessarily NULL terminated! */
} ntp_control_message;

/* this is an association/status-word pair found in control packet reponses */
typedef struct {
	uint16_t assoc;
	uint16_t status;
} ntp_assoc_status_pair;

/* bits 1,2 are the leap indicator */
#define LI_MASK 0xc0
#define LI(x) ((x&LI_MASK)>>6)
#define LI_SET(x,y) do{ x |= ((y<<6)&LI_MASK); }while(0)
/* and these are the values of the leap indicator */
#define LI_NOWARNING 0x00
#define LI_EXTRASEC 0x01
#define LI_MISSINGSEC 0x02
#define LI_ALARM 0x03
/* bits 3,4,5 are the ntp version */
#define VN_MASK 0x38
#define VN(x)	((x&VN_MASK)>>3)
#define VN_SET(x,y)	do{ x |= ((y<<3)&VN_MASK); }while(0)
#define VN_RESERVED 0x02
/* bits 6,7,8 are the ntp mode */
#define MODE_MASK 0x07
#define MODE(x) (x&MODE_MASK)
#define MODE_SET(x,y)	do{ x |= (y&MODE_MASK); }while(0)
/* here are some values */
#define MODE_CLIENT 0x03
#define MODE_CONTROLMSG 0x06
/* In control message, bits 8-10 are R,E,M bits */
#define REM_MASK 0xe0
#define REM_RESP 0x80
#define REM_ERROR 0x40
#define REM_MORE 0x20
/* In control message, bits 11 - 15 are opcode */
#define OP_MASK 0x1f
#define OP_SET(x,y)   do{ x |= (y&OP_MASK); }while(0)
#define OP_READSTAT 0x01
#define OP_READVAR  0x02
/* In peer status bytes, bits 6,7,8 determine clock selection status */
#define PEER_SEL(x) ((ntohs(x)>>8)&0x07)
#define PEER_TRUECHIMER 0x02
#define PEER_INCLUDED 0x04
#define PEER_SYNCSOURCE 0x06

/* NTP control message header is 12 bytes, plus any data in the data
 * field, plus null padding to the nearest 32-bit boundary per rfc.
 */
#define SIZEOF_NTPCM(m) (12+ntohs(m.count)+((ntohs(m.count)%4)?4-(ntohs(m.count)%4):0))

/* finally, a little helper or two for debugging: */
#define DBG(x) do{if(verbose>1){ x; }}while(0);
#define PRINTSOCKADDR(x) \
	do{ \
		printf("%u.%u.%u.%u", (x>>24)&0xff, (x>>16)&0xff, (x>>8)&0xff, x&0xff);\
	}while(0);

void print_ntp_control_message(const ntp_control_message *p){
	int i=0, numpeers=0;
	const ntp_assoc_status_pair *peer=NULL;

	printf("control packet contents:\n");
	printf("\tflags: 0x%.2x , 0x%.2x\n", p->flags, p->op);
	printf("\t  li=%d (0x%.2x)\n", LI(p->flags), p->flags&LI_MASK);
	printf("\t  vn=%d (0x%.2x)\n", VN(p->flags), p->flags&VN_MASK);
	printf("\t  mode=%d (0x%.2x)\n", MODE(p->flags), p->flags&MODE_MASK);
	printf("\t  response=%d (0x%.2x)\n", (p->op&REM_RESP)>0, p->op&REM_RESP);
	printf("\t  more=%d (0x%.2x)\n", (p->op&REM_MORE)>0, p->op&REM_MORE);
	printf("\t  error=%d (0x%.2x)\n", (p->op&REM_ERROR)>0, p->op&REM_ERROR);
	printf("\t  op=%d (0x%.2x)\n", p->op&OP_MASK, p->op&OP_MASK);
	printf("\tsequence: %d (0x%.2x)\n", ntohs(p->seq), ntohs(p->seq));
	printf("\tstatus: %d (0x%.2x)\n", ntohs(p->status), ntohs(p->status));
	printf("\tassoc: %d (0x%.2x)\n", ntohs(p->assoc), ntohs(p->assoc));
	printf("\toffset: %d (0x%.2x)\n", ntohs(p->offset), ntohs(p->offset));
	printf("\tcount: %d (0x%.2x)\n", ntohs(p->count), ntohs(p->count));
	numpeers=ntohs(p->count)/(sizeof(ntp_assoc_status_pair));
	if(p->op&REM_RESP && p->op&OP_READSTAT){
		peer=(ntp_assoc_status_pair*)p->data;
		for(i=0;i<numpeers;i++){
			printf("\tpeer id %.2x status %.2x",
			       ntohs(peer[i].assoc), ntohs(peer[i].status));
			if(PEER_SEL(peer[i].status) >= PEER_SYNCSOURCE){
				printf(" <-- current sync source");
			} else if(PEER_SEL(peer[i].status) >= PEER_INCLUDED){
				printf(" <-- current sync candidate");
			} else if(PEER_SEL(peer[i].status) >= PEER_TRUECHIMER){
				printf(" <-- outlyer, but truechimer");
			}
			printf("\n");
		}
	}
}

void
setup_control_request(ntp_control_message *p, uint8_t opcode, uint16_t seq){
	memset(p, 0, sizeof(ntp_control_message));
	LI_SET(p->flags, LI_NOWARNING);
	VN_SET(p->flags, VN_RESERVED);
	MODE_SET(p->flags, MODE_CONTROLMSG);
	OP_SET(p->op, opcode);
	p->seq = htons(seq);
	/* Remaining fields are zero for requests */
}

/* This function does all the actual work; roughly here's what it does
 * beside setting the offest, jitter and stratum passed as argument:
 *  - offset can be negative, so if it cannot get the offset, offset_result
 *    is set to UNKNOWN, otherwise OK.
 *  - jitter and stratum are set to -1 if they cannot be retrieved so any
 *    positive value means a success retrieving the value.
 *  - status is set to WARNING if there's no sync.peer (otherwise OK) and is
 *    the return value of the function.
 *  status is pretty much useless as syncsource_found is a global variable
 *  used later in main to check is the server was synchronized. It works
 *  so I left it alone */
int ntp_request(const char *host, double *offset, int *offset_result, double *jitter, int *stratum, int *num_truechimers){
	int conn=-1, i, npeers=0, num_candidates=0;
	double tmp_offset = 0;
	int min_peer_sel=PEER_INCLUDED;
	int peers_size=0, peer_offset=0;
	int status;
	ntp_assoc_status_pair *peers=NULL;
	ntp_control_message req;
	const char *getvar = "stratum,offset,jitter";
	char *data, *value, *nptr;
	void *tmp;

	status = STATE_OK;
	*offset_result = STATE_UNKNOWN;
	*jitter = *stratum = -1;
	*num_truechimers = 0;

	/* Long-winded explanation:
	 * Getting the sync peer offset, jitter and stratum requires a number of
	 * steps:
	 * 1) Send a READSTAT request.
	 * 2) Interpret the READSTAT reply
	 *  a) The data section contains a list of peer identifiers (16 bits)
	 *     and associated status words (16 bits)
	 *  b) We want the value of 0x06 in the SEL (peer selection) value,
	 *     which means "current synchronizatin source".  If that's missing,
	 *     we take anything better than 0x04 (see the rfc for details) but
	 *     set a minimum of warning.
	 * 3) Send a READVAR request for information on each peer identified
	 *    in 2b greater than the minimum selection value.
	 * 4) Extract the offset, jitter and stratum value from the data[]
	 *    (it's ASCII)
	 */
	my_udp_connect(server_address, port, &conn);

	/* keep sending requests until the server stops setting the
	 * REM_MORE bit, though usually this is only 1 packet. */
	do{
		setup_control_request(&req, OP_READSTAT, 1);
		DBG(printf("sending READSTAT request"));
		write(conn, &req, SIZEOF_NTPCM(req));
		DBG(print_ntp_control_message(&req));

		do {
			/* Attempt to read the largest size packet possible */
			req.count=htons(MAX_CM_SIZE);
			DBG(printf("recieving READSTAT response"))
			if(read(conn, &req, SIZEOF_NTPCM(req)) == -1)
				die(STATE_CRITICAL, "NTP CRITICAL: No response from NTP server\n");
			DBG(print_ntp_control_message(&req));
			/* discard obviously invalid packets */
			if (ntohs(req.count) > MAX_CM_SIZE)
				die(STATE_CRITICAL, "NTP CRITICAL: Invalid packet received from NTP server\n");
		} while (!(req.op&OP_READSTAT && ntohs(req.seq) == 1));

		if (LI(req.flags) == LI_ALARM) li_alarm = 1;
		/* Each peer identifier is 4 bytes in the data section, which
	 	 * we represent as a ntp_assoc_status_pair datatype.
	 	 */
		peers_size+=ntohs(req.count);
		if((tmp=realloc(peers, peers_size)) == NULL)
			free(peers), die(STATE_UNKNOWN, "can not (re)allocate 'peers' buffer\n");
		peers=tmp;
		memcpy((void*)((ptrdiff_t)peers+peer_offset), (void*)req.data, ntohs(req.count));
		npeers=peers_size/sizeof(ntp_assoc_status_pair);
		peer_offset+=ntohs(req.count);
	} while(req.op&REM_MORE);

	/* first, let's find out if we have a sync source, or if there are
	 * at least some candidates. In the latter case we'll issue
	 * a warning but go ahead with the check on them. */
	for (i = 0; i < npeers; i++){
		if(PEER_SEL(peers[i].status) >= PEER_TRUECHIMER){
			(*num_truechimers)++;
			if(PEER_SEL(peers[i].status) >= PEER_INCLUDED){
				num_candidates++;
				if(PEER_SEL(peers[i].status) >= PEER_SYNCSOURCE){
					syncsource_found=1;
					min_peer_sel=PEER_SYNCSOURCE;
				}
			}
		}
	}
	if(verbose) printf("%d candidate peers available\n", num_candidates);
	if(verbose && syncsource_found) printf("synchronization source found\n");
	if(! syncsource_found){
		status = STATE_WARNING;
		if(verbose) printf("warning: no synchronization source found\n");
	}
	if(li_alarm){
		status = STATE_WARNING;
		if(verbose) printf("warning: LI_ALARM bit is set\n");
	}


	for (i = 0; i < npeers; i++){
		/* Only query this server if it is the current sync source */
		/* If there's no sync.peer, query all candidates and use the best one */
		if (PEER_SEL(peers[i].status) >= min_peer_sel){
			if(verbose) printf("Getting offset, jitter and stratum for peer %.2x\n", ntohs(peers[i].assoc));
			xasprintf(&data, "");
			do{
				setup_control_request(&req, OP_READVAR, 2);
				req.assoc = peers[i].assoc;
				/* Putting the wanted variable names in the request
				 * cause the server to provide _only_ the requested values.
				 * thus reducing net traffic, guaranteeing us only a single
				 * datagram in reply, and making intepretation much simpler
				 */
				/* Older servers doesn't know what jitter is, so if we get an
				 * error on the first pass we redo it with "dispersion" */
				strncpy(req.data, getvar, MAX_CM_SIZE-1);
				req.count = htons(strlen(getvar));
				DBG(printf("sending READVAR request...\n"));
				write(conn, &req, SIZEOF_NTPCM(req));
				DBG(print_ntp_control_message(&req));

				do {
					req.count = htons(MAX_CM_SIZE);
					DBG(printf("receiving READVAR response...\n"));
					read(conn, &req, SIZEOF_NTPCM(req));
					DBG(print_ntp_control_message(&req));
				} while (!(req.op&OP_READVAR && ntohs(req.seq) == 2));

				if(!(req.op&REM_ERROR))
					xasprintf(&data, "%s%s", data, req.data);
			} while(req.op&REM_MORE);

			if(req.op&REM_ERROR) {
				if(strstr(getvar, "jitter")) {
					if(verbose) printf("The command failed. This is usually caused by servers refusing the 'jitter'\nvariable. Restarting with 'dispersion'...\n");
					getvar = "stratum,offset,dispersion";
					i--;
					continue;
				} else if(strlen(getvar)) {
					if(verbose) printf("Server didn't like dispersion either; will retrieve everything\n");
					getvar = "";
					i--;
					continue;
				}
			}

			if(verbose > 1)
				printf("Server responded: >>>%s<<<\n", data);

			/* get the offset */
			if(verbose)
				printf("parsing offset from peer %.2x: ", ntohs(peers[i].assoc));

			value = np_extract_ntpvar(data, "offset");
			nptr=NULL;
			/* Convert the value if we have one */
			if(value != NULL)
				tmp_offset = strtod(value, &nptr) / 1000;
			/* If value is null or no conversion was performed */
			if(value == NULL || value==nptr) {
				if(verbose) printf("error: unable to read server offset response.\n");
			} else {
				if(verbose) printf("%.10g\n", tmp_offset);
				if(*offset_result == STATE_UNKNOWN || fabs(tmp_offset) < fabs(*offset)) {
					*offset = tmp_offset;
					*offset_result = STATE_OK;
				} else {
					/* Skip this one; move to the next */
					continue;
				}
			}

			if(do_jitter) {
				/* get the jitter */
				if(verbose) {
					printf("parsing %s from peer %.2x: ", strstr(getvar, "dispersion") != NULL ? "dispersion" : "jitter", ntohs(peers[i].assoc));
				}
				value = np_extract_ntpvar(data, strstr(getvar, "dispersion") != NULL ? "dispersion" : "jitter");
				nptr=NULL;
				/* Convert the value if we have one */
				if(value != NULL)
					*jitter = strtod(value, &nptr);
				/* If value is null or no conversion was performed */
				if(value == NULL || value==nptr) {
					if(verbose) printf("error: unable to read server jitter/dispersion response.\n");
					*jitter = -1;
				} else if(verbose) {
					printf("%.10g\n", *jitter);
				}
			}

			if(do_stratum) {
				/* get the stratum */
				if(verbose) {
					printf("parsing stratum from peer %.2x: ", ntohs(peers[i].assoc));
				}
				value = np_extract_ntpvar(data, "stratum");
				nptr=NULL;
				/* Convert the value if we have one */
				if(value != NULL)
					*stratum = strtol(value, &nptr, 10);
				if(value == NULL || value==nptr) {
					if(verbose) printf("error: unable to read server stratum response.\n");
					*stratum = -1;
				} else {
					if(verbose) printf("%i\n", *stratum);
				}
			}
		} /* if (PEER_SEL(peers[i].status) >= min_peer_sel) */
	} /* for (i = 0; i < npeers; i++) */

	close(conn);
	if(peers!=NULL) free(peers);

	return status;
}

int process_arguments(int argc, char **argv){
	int c;
	int option=0;
	static struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"use-ipv4", no_argument, 0, '4'},
		{"use-ipv6", no_argument, 0, '6'},
		{"quiet", no_argument, 0, 'q'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"swarn", required_argument, 0, 'W'},
		{"scrit", required_argument, 0, 'C'},
		{"jwarn", required_argument, 0, 'j'},
		{"jcrit", required_argument, 0, 'k'},
		{"twarn", required_argument, 0, 'm'},
		{"tcrit", required_argument, 0, 'n'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};


	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "Vhv46qw:c:W:C:j:k:m:n:t:H:p:", longopts, &option);
		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'h':
			print_help();
			exit(STATE_OK);
			break;
		case 'V':
			print_revision(progname, NP_VERSION);
			exit(STATE_OK);
			break;
		case 'v':
			verbose++;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'w':
			do_offset=1;
			owarn = optarg;
			break;
		case 'c':
			do_offset=1;
			ocrit = optarg;
			break;
		case 'W':
			do_stratum=1;
			swarn = optarg;
			break;
		case 'C':
			do_stratum=1;
			scrit = optarg;
			break;
		case 'j':
			do_jitter=1;
			jwarn = optarg;
			break;
		case 'k':
			do_jitter=1;
			jcrit = optarg;
			break;
		case 'm':
			do_truechimers=1;
			twarn = optarg;
			break;
		case 'n':
			do_truechimers=1;
			tcrit = optarg;
			break;
		case 'H':
			if(is_host(optarg) == FALSE)
				usage2(_("Invalid hostname/address"), optarg);
			server_address = strdup(optarg);
			break;
		case 'p':
			port=atoi(optarg);
			break;
		case 't':
			socket_timeout=atoi(optarg);
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
		case '?':
			/* print short usage statement if args not parsable */
			usage5 ();
			break;
		}
	}

	if(server_address == NULL){
		usage4(_("Hostname was not supplied"));
	}

	return 0;
}

char *perfd_offset (double offset)
{
	return fperfdata ("offset", offset, "s",
		TRUE, offset_thresholds->warning->end,
		TRUE, offset_thresholds->critical->end,
		FALSE, 0, FALSE, 0);
}

char *perfd_jitter (double jitter)
{
	return fperfdata ("jitter", jitter, "",
		do_jitter, jitter_thresholds->warning->end,
		do_jitter, jitter_thresholds->critical->end,
		TRUE, 0, FALSE, 0);
}

char *perfd_stratum (int stratum)
{
	return perfdata ("stratum", stratum, "",
		do_stratum, (int)stratum_thresholds->warning->end,
		do_stratum, (int)stratum_thresholds->critical->end,
		TRUE, 0, TRUE, 16);
}

char *perfd_truechimers (int num_truechimers)
{
	return perfdata ("truechimers", num_truechimers, "",
		do_truechimers, (int)truechimer_thresholds->warning->end,
		do_truechimers, (int)truechimer_thresholds->critical->end,
		TRUE, 0, FALSE, 0);
}

int main(int argc, char *argv[]){
	int result, offset_result, stratum, num_truechimers;
	double offset=0, jitter=0;
	char *result_line, *perfdata_line;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	set_thresholds(&offset_thresholds, owarn, ocrit);
	set_thresholds(&jitter_thresholds, jwarn, jcrit);
	set_thresholds(&stratum_thresholds, swarn, scrit);
	set_thresholds(&truechimer_thresholds, twarn, tcrit);

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	/* This returns either OK or WARNING (See comment preceeding ntp_request) */
	result = ntp_request(server_address, &offset, &offset_result, &jitter, &stratum, &num_truechimers);

	if(offset_result == STATE_UNKNOWN) {
		/* if there's no sync peer (this overrides ntp_request output): */
		result = (quiet == 1 ? STATE_UNKNOWN : STATE_CRITICAL);
	} else {
		/* Be quiet if there's no candidates either */
		if (quiet == 1 && result == STATE_WARNING)
			result = STATE_UNKNOWN;
		result = max_state_alt(result, get_status(fabs(offset), offset_thresholds));
	}

	if(do_truechimers)
		result = max_state_alt(result, get_status(num_truechimers, truechimer_thresholds));

	if(do_stratum)
		result = max_state_alt(result, get_status(stratum, stratum_thresholds));

	if(do_jitter)
		result = max_state_alt(result, get_status(jitter, jitter_thresholds));

	switch (result) {
		case STATE_CRITICAL :
			xasprintf(&result_line, _("NTP CRITICAL:"));
			break;
		case STATE_WARNING :
			xasprintf(&result_line, _("NTP WARNING:"));
			break;
		case STATE_OK :
			xasprintf(&result_line, _("NTP OK:"));
			break;
		default :
			xasprintf(&result_line, _("NTP UNKNOWN:"));
			break;
	}
	if(!syncsource_found)
		xasprintf(&result_line, "%s %s,", result_line, _("Server not synchronized"));
	else if(li_alarm)
		xasprintf(&result_line, "%s %s,", result_line, _("Server has the LI_ALARM bit set"));

	if(offset_result == STATE_UNKNOWN){
		xasprintf(&result_line, "%s %s", result_line, _("Offset unknown"));
		xasprintf(&perfdata_line, "");
	} else {
		xasprintf(&result_line, "%s %s %.10g secs", result_line, _("Offset"), offset);
		xasprintf(&perfdata_line, "%s", perfd_offset(offset));
	}
	if (do_jitter) {
		xasprintf(&result_line, "%s, jitter=%f", result_line, jitter);
		xasprintf(&perfdata_line, "%s %s", perfdata_line, perfd_jitter(jitter));
	}
	if (do_stratum) {
		xasprintf(&result_line, "%s, stratum=%i", result_line, stratum);
		xasprintf(&perfdata_line, "%s %s", perfdata_line, perfd_stratum(stratum));
	}
	if (do_truechimers) {
		xasprintf(&result_line, "%s, truechimers=%i", result_line, num_truechimers);
		xasprintf(&perfdata_line, "%s %s", perfdata_line, perfd_truechimers(num_truechimers));
	}
	printf("%s|%s\n", result_line, perfdata_line);

	if(server_address!=NULL) free(server_address);
	return result;
}



void print_help(void){
	print_revision(progname, NP_VERSION);

	printf ("Copyright (c) 2006 Sean Finney\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin checks the selected ntp server"));

	printf ("\n\n");

	print_usage();
	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);
	printf (UT_IPv46);
	printf (UT_HOST_PORT, 'p', "123");
	printf (" %s\n", "-q, --quiet");
	printf ("    %s\n", _("Returns UNKNOWN instead of CRITICAL or WARNING if server isn't synchronized"));
	printf (" %s\n", "-w, --warning=THRESHOLD");
	printf ("    %s\n", _("Offset to result in warning status (seconds)"));
	printf (" %s\n", "-c, --critical=THRESHOLD");
	printf ("    %s\n", _("Offset to result in critical status (seconds)"));
	printf (" %s\n", "-W, --swarn=THRESHOLD");
	printf ("    %s\n", _("Warning threshold for stratum of server's synchronization peer"));
	printf (" %s\n", "-C, --scrit=THRESHOLD");
	printf ("    %s\n", _("Critical threshold for stratum of server's synchronization peer"));
	printf (" %s\n", "-j, --jwarn=THRESHOLD");
	printf ("    %s\n", _("Warning threshold for jitter"));
	printf (" %s\n", "-k, --jcrit=THRESHOLD");
	printf ("    %s\n", _("Critical threshold for jitter"));
	printf (" %s\n", "-m, --twarn=THRESHOLD");
	printf ("    %s\n", _("Warning threshold for number of usable time sources (\"truechimers\")"));
	printf (" %s\n", "-n, --tcrit=THRESHOLD");
	printf ("    %s\n", _("Critical threshold for number of usable time sources (\"truechimers\")"));
	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf (UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("This plugin checks an NTP server independent of any commandline"));
	printf("%s\n\n", _("programs or external libraries."));

	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("Use this plugin to check the health of an NTP server. It supports"));
	printf(" %s\n", _("checking the offset with the sync peer, the jitter and stratum. This"));
	printf(" %s\n", _("plugin will not check the clock offset between the local host and NTP"));
	printf(" %s\n", _("server; please use check_ntp_time for that purpose."));
	printf("\n");
	printf(UT_THRESHOLDS_NOTES);

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf(" %s\n", _("Simple NTP server check:"));
	printf("  %s\n", ("./check_ntp_peer -H ntpserv -w 0.5 -c 1"));
	printf("\n");
	printf(" %s\n", _("Check jitter too, avoiding critical notifications if jitter isn't available"));
	printf(" %s\n", _("(See Notes above for more details on thresholds formats):"));
	printf("  %s\n", ("./check_ntp_peer -H ntpserv -w 0.5 -c 1 -j -1:100 -k -1:200"));
	printf("\n");
	printf(" %s\n", _("Only check the number of usable time sources (\"truechimers\"):"));
	printf("  %s\n", ("./check_ntp_peer -H ntpserv -m @5 -n @3"));
	printf("\n");
	printf(" %s\n", _("Check only stratum:"));
	printf("  %s\n", ("./check_ntp_peer -H ntpserv -W 4 -C 6"));

	printf (UT_SUPPORT);
}

void
print_usage(void)
{
	printf ("%s\n", _("Usage:"));
	printf(" %s -H <host> [-4|-6] [-w <warn>] [-c <crit>] [-W <warn>] [-C <crit>]\n", progname);
	printf("       [-j <warn>] [-k <crit>] [-v verbose]\n");
}
