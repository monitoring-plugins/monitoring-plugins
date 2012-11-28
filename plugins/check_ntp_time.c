/*****************************************************************************
* 
* Nagios check_ntp_time plugin
* 
* License: GPL
* Copyright (c) 2006 Sean Finney <seanius@seanius.net>
* Copyright (c) 2006-2008 Nagios Plugins Development Team
* 
* Description:
* 
* This file contains the check_ntp_time plugin
* 
* This plugin checks the clock offset between the local host and a
* remote NTP server. It is independent of any commandline programs or
* external libraries.
* 
* If you'd rather want to monitor an NTP server, please use
* check_ntp_peer.
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

const char *progname = "check_ntp_time";
const char *copyright = "2006-2008";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

static char *server_address=NULL;
static char *port="123";
static int verbose=0;
static int quiet=0;
static char *owarn="60";
static char *ocrit="120";

int process_arguments (int, char **);
thresholds *offset_thresholds = NULL;
void print_help (void);
void print_usage (void);

/* number of times to perform each request to get a good average. */
#define AVG_NUM 4

/* max size of control message data */
#define MAX_CM_SIZE 468

/* this structure holds everything in an ntp request/response as per rfc1305 */
typedef struct {
	uint8_t flags;       /* byte with leapindicator,vers,mode. see macros */
	uint8_t stratum;     /* clock stratum */
	int8_t poll;         /* polling interval */
	int8_t precision;    /* precision of the local clock */
	int32_t rtdelay;     /* total rt delay, as a fixed point num. see macros */
	uint32_t rtdisp;     /* like above, but for max err to primary src */
	uint32_t refid;      /* ref clock identifier */
	uint64_t refts;      /* reference timestamp.  local time local clock */
	uint64_t origts;     /* time at which request departed client */
	uint64_t rxts;       /* time at which request arrived at server */
	uint64_t txts;       /* time at which request departed server */
} ntp_message;

/* this structure holds data about results from querying offset from a peer */
typedef struct {
	time_t waiting;         /* ts set when we started waiting for a response */
	int num_responses;      /* number of successfully recieved responses */
	uint8_t stratum;        /* copied verbatim from the ntp_message */
	double rtdelay;         /* converted from the ntp_message */
	double rtdisp;          /* converted from the ntp_message */
	double offset[AVG_NUM]; /* offsets from each response */
	uint8_t flags;       /* byte with leapindicator,vers,mode. see macros */
} ntp_server_results;

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
#define PEER_INCLUDED 0x04
#define PEER_SYNCSOURCE 0x06

/**
 ** a note about the 32-bit "fixed point" numbers:
 **
 they are divided into halves, each being a 16-bit int in network byte order:
 - the first 16 bits are an int on the left side of a decimal point.
 - the second 16 bits represent a fraction n/(2^16)
 likewise for the 64-bit "fixed point" numbers with everything doubled :)
 **/

/* macros to access the left/right 16 bits of a 32-bit ntp "fixed point"
   number.  note that these can be used as lvalues too */
#define L16(x) (((uint16_t*)&x)[0])
#define R16(x) (((uint16_t*)&x)[1])
/* macros to access the left/right 32 bits of a 64-bit ntp "fixed point"
   number.  these too can be used as lvalues */
#define L32(x) (((uint32_t*)&x)[0])
#define R32(x) (((uint32_t*)&x)[1])

/* ntp wants seconds since 1/1/00, epoch is 1/1/70.  this is the difference */
#define EPOCHDIFF 0x83aa7e80UL

/* extract a 32-bit ntp fixed point number into a double */
#define NTP32asDOUBLE(x) (ntohs(L16(x)) + (double)ntohs(R16(x))/65536.0)

/* likewise for a 64-bit ntp fp number */
#define NTP64asDOUBLE(n) (double)(((uint64_t)n)?\
                         (ntohl(L32(n))-EPOCHDIFF) + \
                         (.00000001*(0.5+(double)(ntohl(R32(n))/42.94967296))):\
                         0)

/* convert a struct timeval to a double */
#define TVasDOUBLE(x) (double)(x.tv_sec+(0.000001*x.tv_usec))

/* convert an ntp 64-bit fp number to a struct timeval */
#define NTP64toTV(n,t) \
	do{ if(!n) t.tv_sec = t.tv_usec = 0; \
	    else { \
			t.tv_sec=ntohl(L32(n))-EPOCHDIFF; \
			t.tv_usec=(int)(0.5+(double)(ntohl(R32(n))/4294.967296)); \
		} \
	}while(0)

/* convert a struct timeval to an ntp 64-bit fp number */
#define TVtoNTP64(t,n) \
	do{ if(!t.tv_usec && !t.tv_sec) n=0x0UL; \
		else { \
			L32(n)=htonl(t.tv_sec + EPOCHDIFF); \
			R32(n)=htonl((uint64_t)((4294.967296*t.tv_usec)+.5)); \
		} \
	} while(0)

/* NTP control message header is 12 bytes, plus any data in the data
 * field, plus null padding to the nearest 32-bit boundary per rfc.
 */
#define SIZEOF_NTPCM(m) (12+ntohs(m.count)+((m.count)?4-(ntohs(m.count)%4):0))

/* finally, a little helper or two for debugging: */
#define DBG(x) do{if(verbose>1){ x; }}while(0);
#define PRINTSOCKADDR(x) \
	do{ \
		printf("%u.%u.%u.%u", (x>>24)&0xff, (x>>16)&0xff, (x>>8)&0xff, x&0xff);\
	}while(0);

/* calculate the offset of the local clock */
static inline double calc_offset(const ntp_message *m, const struct timeval *t){
	double client_tx, peer_rx, peer_tx, client_rx;
	client_tx = NTP64asDOUBLE(m->origts);
	peer_rx = NTP64asDOUBLE(m->rxts);
	peer_tx = NTP64asDOUBLE(m->txts);
	client_rx=TVasDOUBLE((*t));
	return (.5*((peer_tx-client_rx)+(peer_rx-client_tx)));
}

/* print out a ntp packet in human readable/debuggable format */
void print_ntp_message(const ntp_message *p){
	struct timeval ref, orig, rx, tx;

	NTP64toTV(p->refts,ref);
	NTP64toTV(p->origts,orig);
	NTP64toTV(p->rxts,rx);
	NTP64toTV(p->txts,tx);

	printf("packet contents:\n");
	printf("\tflags: 0x%.2x\n", p->flags);
	printf("\t  li=%d (0x%.2x)\n", LI(p->flags), p->flags&LI_MASK);
	printf("\t  vn=%d (0x%.2x)\n", VN(p->flags), p->flags&VN_MASK);
	printf("\t  mode=%d (0x%.2x)\n", MODE(p->flags), p->flags&MODE_MASK);
	printf("\tstratum = %d\n", p->stratum);
	printf("\tpoll = %g\n", pow(2, p->poll));
	printf("\tprecision = %g\n", pow(2, p->precision));
	printf("\trtdelay = %-.16g\n", NTP32asDOUBLE(p->rtdelay));
	printf("\trtdisp = %-.16g\n", NTP32asDOUBLE(p->rtdisp));
	printf("\trefid = %x\n", p->refid);
	printf("\trefts = %-.16g\n", NTP64asDOUBLE(p->refts));
	printf("\torigts = %-.16g\n", NTP64asDOUBLE(p->origts));
	printf("\trxts = %-.16g\n", NTP64asDOUBLE(p->rxts));
	printf("\ttxts = %-.16g\n", NTP64asDOUBLE(p->txts));
}

void setup_request(ntp_message *p){
	struct timeval t;

	memset(p, 0, sizeof(ntp_message));
	LI_SET(p->flags, LI_ALARM);
	VN_SET(p->flags, 4);
	MODE_SET(p->flags, MODE_CLIENT);
	p->poll=4;
	p->precision=(int8_t)0xfa;
	L16(p->rtdelay)=htons(1);
	L16(p->rtdisp)=htons(1);

	gettimeofday(&t, NULL);
	TVtoNTP64(t,p->txts);
}

/* select the "best" server from a list of servers, and return its index.
 * this is done by filtering servers based on stratum, dispersion, and
 * finally round-trip delay. */
int best_offset_server(const ntp_server_results *slist, int nservers){
	int i=0, cserver=0, best_server=-1;

	/* for each server */
	for(cserver=0; cserver<nservers; cserver++){
		/* We don't want any servers that fails these tests */
		/* Sort out servers that didn't respond or responede with a 0 stratum;
		 * stratum 0 is for reference clocks so no NTP server should ever report
		 * a stratum 0 */
		if ( slist[cserver].stratum == 0){
			if (verbose) printf("discarding peer %d: stratum=%d\n", cserver, slist[cserver].stratum);
			continue;
		}
		/* Sort out servers with error flags */
		if ( LI(slist[cserver].flags) == LI_ALARM ){
			if (verbose) printf("discarding peer %d: flags=%d\n", cserver, LI(slist[cserver].flags));
			continue;
		}

		/* If we don't have a server yet, use the first one */
		if (best_server == -1) {
			best_server = cserver;
			DBG(printf("using peer %d as our first candidate\n", best_server));
			continue;
		}

		/* compare the server to the best one we've seen so far */
		/* does it have an equal or better stratum? */
		DBG(printf("comparing peer %d with peer %d\n", cserver, best_server));
		if(slist[cserver].stratum <= slist[best_server].stratum){
			DBG(printf("stratum for peer %d <= peer %d\n", cserver, best_server));
			/* does it have an equal or better dispersion? */
			if(slist[cserver].rtdisp <= slist[best_server].rtdisp){
				DBG(printf("dispersion for peer %d <= peer %d\n", cserver, best_server));
				/* does it have a better rtdelay? */
				if(slist[cserver].rtdelay < slist[best_server].rtdelay){
					DBG(printf("rtdelay for peer %d < peer %d\n", cserver, best_server));
					best_server = cserver;
					DBG(printf("peer %d is now our best candidate\n", best_server));
				}
			}
		}
	}

	if(best_server >= 0) {
		DBG(printf("best server selected: peer %d\n", best_server));
		return best_server;
	} else {
		DBG(printf("no peers meeting synchronization criteria :(\n"));
		return -1;
	}
}

/* do everything we need to get the total average offset
 * - we use a certain amount of parallelization with poll() to ensure
 *   we don't waste time sitting around waiting for single packets.
 * - we also "manually" handle resolving host names and connecting, because
 *   we have to do it in a way that our lazy macros don't handle currently :( */
double offset_request(const char *host, int *status){
	int i=0, j=0, ga_result=0, num_hosts=0, *socklist=NULL, respnum=0;
	int servers_completed=0, one_written=0, one_read=0, servers_readable=0, best_index=-1;
	time_t now_time=0, start_ts=0;
	ntp_message *req=NULL;
	double avg_offset=0.;
	struct timeval recv_time;
	struct addrinfo *ai=NULL, *ai_tmp=NULL, hints;
	struct pollfd *ufds=NULL;
	ntp_server_results *servers=NULL;

	/* setup hints to only return results from getaddrinfo that we'd like */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = address_family;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_socktype = SOCK_DGRAM;

	/* fill in ai with the list of hosts resolved by the host name */
	ga_result = getaddrinfo(host, port, &hints, &ai);
	if(ga_result!=0){
		die(STATE_UNKNOWN, "error getting address for %s: %s\n",
		    host, gai_strerror(ga_result));
	}

	/* count the number of returned hosts, and allocate stuff accordingly */
	for(ai_tmp=ai; ai_tmp!=NULL; ai_tmp=ai_tmp->ai_next){ num_hosts++; }
	req=(ntp_message*)malloc(sizeof(ntp_message)*num_hosts);
	if(req==NULL) die(STATE_UNKNOWN, "can not allocate ntp message array");
	socklist=(int*)malloc(sizeof(int)*num_hosts);
	if(socklist==NULL) die(STATE_UNKNOWN, "can not allocate socket array");
	ufds=(struct pollfd*)malloc(sizeof(struct pollfd)*num_hosts);
	if(ufds==NULL) die(STATE_UNKNOWN, "can not allocate socket array");
	servers=(ntp_server_results*)malloc(sizeof(ntp_server_results)*num_hosts);
	if(servers==NULL) die(STATE_UNKNOWN, "can not allocate server array");
	memset(servers, 0, sizeof(ntp_server_results)*num_hosts);
	DBG(printf("Found %d peers to check\n", num_hosts));

	/* setup each socket for writing, and the corresponding struct pollfd */
	ai_tmp=ai;
	for(i=0;ai_tmp;i++){
		socklist[i]=socket(ai_tmp->ai_family, SOCK_DGRAM, IPPROTO_UDP);
		if(socklist[i] == -1) {
			perror(NULL);
			die(STATE_UNKNOWN, "can not create new socket");
		}
		if(connect(socklist[i], ai_tmp->ai_addr, ai_tmp->ai_addrlen)){
			/* don't die here, because it is enough if there is one server
			   answering in time. This also would break for dual ipv4/6 stacked
			   ntp servers when the client only supports on of them.
			 */
			DBG(printf("can't create socket connection on peer %i: %s\n", i, strerror(errno)));
		} else {
			ufds[i].fd=socklist[i];
			ufds[i].events=POLLIN;
			ufds[i].revents=0;
		}
		ai_tmp = ai_tmp->ai_next;
	}

	/* now do AVG_NUM checks to each host. We stop before timeout/2 seconds
	 * have passed in order to ensure post-processing and jitter time. */
	now_time=start_ts=time(NULL);
	while(servers_completed<num_hosts && now_time-start_ts <= socket_timeout/2){
		/* loop through each server and find each one which hasn't
		 * been touched in the past second or so and is still lacking
		 * some responses. For each of these servers, send a new request,
		 * and update the "waiting" timestamp with the current time. */
		one_written=0;
		now_time=time(NULL);

		for(i=0; i<num_hosts; i++){
			if(servers[i].waiting<now_time && servers[i].num_responses<AVG_NUM){
				if(verbose && servers[i].waiting != 0) printf("re-");
				if(verbose) printf("sending request to peer %d\n", i);
				setup_request(&req[i]);
				write(socklist[i], &req[i], sizeof(ntp_message));
				servers[i].waiting=now_time;
				one_written=1;
				break;
			}
		}

		/* quickly poll for any sockets with pending data */
		servers_readable=poll(ufds, num_hosts, 100);
		if(servers_readable==-1){
			perror("polling ntp sockets");
			die(STATE_UNKNOWN, "communication errors");
		}

		/* read from any sockets with pending data */
		for(i=0; servers_readable && i<num_hosts; i++){
			if(ufds[i].revents&POLLIN && servers[i].num_responses < AVG_NUM){
				if(verbose) {
					printf("response from peer %d: ", i);
				}

				read(ufds[i].fd, &req[i], sizeof(ntp_message));
				gettimeofday(&recv_time, NULL);
				DBG(print_ntp_message(&req[i]));
				respnum=servers[i].num_responses++;
				servers[i].offset[respnum]=calc_offset(&req[i], &recv_time);
				if(verbose) {
					printf("offset %.10g\n", servers[i].offset[respnum]);
				}
				servers[i].stratum=req[i].stratum;
				servers[i].rtdisp=NTP32asDOUBLE(req[i].rtdisp);
				servers[i].rtdelay=NTP32asDOUBLE(req[i].rtdelay);
				servers[i].waiting=0;
				servers[i].flags=req[i].flags;
				servers_readable--;
				one_read = 1;
				if(servers[i].num_responses==AVG_NUM) servers_completed++;
			}
		}
		/* lather, rinse, repeat. */
	}

	if (one_read == 0) {
		die(STATE_CRITICAL, "NTP CRITICAL: No response from NTP server\n");
	}

	/* now, pick the best server from the list */
	best_index=best_offset_server(servers, num_hosts);
	if(best_index < 0){
		*status=STATE_UNKNOWN;
	} else {
		/* finally, calculate the average offset */
		for(i=0; i<servers[best_index].num_responses;i++){
			avg_offset+=servers[best_index].offset[j];
		}
		avg_offset/=servers[best_index].num_responses;
	}

	/* cleanup */
	for(j=0; j<num_hosts; j++){ close(socklist[j]); }
	free(socklist);
	free(ufds);
	free(servers);
	free(req);
	freeaddrinfo(ai);

	if(verbose) printf("overall average offset: %.10g\n", avg_offset);
	return avg_offset;
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
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{"port", required_argument, 0, 'p'},
		{0, 0, 0, 0}
	};


	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "Vhv46qw:c:t:H:p:", longopts, &option);
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
			owarn = optarg;
			break;
		case 'c':
			ocrit = optarg;
			break;
		case 'H':
			if(is_host(optarg) == FALSE)
				usage2(_("Invalid hostname/address"), optarg);
			server_address = strdup(optarg);
			break;
		case 'p':
			port = strdup(optarg);
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

int main(int argc, char *argv[]){
	int result, offset_result;
	double offset=0;
	char *result_line, *perfdata_line;

	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	result = offset_result = STATE_OK;

	/* Parse extra opts if any */
	argv=np_extra_opts (&argc, argv, progname);

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	set_thresholds(&offset_thresholds, owarn, ocrit);

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	offset = offset_request(server_address, &offset_result);
	if (offset_result == STATE_UNKNOWN) {
		result = (quiet == 1 ? STATE_UNKNOWN : STATE_CRITICAL);
	} else {
		result = get_status(fabs(offset), offset_thresholds);
	}

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
	if(offset_result == STATE_UNKNOWN){
		xasprintf(&result_line, "%s %s", result_line, _("Offset unknown"));
		xasprintf(&perfdata_line, "");
	} else {
		xasprintf(&result_line, "%s %s %.10g secs", result_line, _("Offset"), offset);
		xasprintf(&perfdata_line, "%s", perfd_offset(offset));
	}
	printf("%s|%s\n", result_line, perfdata_line);

	if(server_address!=NULL) free(server_address);
	return result;
}

void print_help(void){
	print_revision(progname, NP_VERSION);

	printf ("Copyright (c) 2006 Sean Finney\n");
	printf (COPYRIGHT, copyright, email);

	printf ("%s\n", _("This plugin checks the clock offset with the ntp server"));

	printf ("\n\n");

	print_usage();
	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);
	printf (UT_IPv46);
	printf (UT_HOST_PORT, 'p', "123");
	printf (" %s\n", "-q, --quiet");
	printf ("    %s\n", _("Returns UNKNOWN instead of CRITICAL if offset cannot be found"));
	printf (" %s\n", "-w, --warning=THRESHOLD");
	printf ("    %s\n", _("Offset to result in warning status (seconds)"));
	printf (" %s\n", "-c, --critical=THRESHOLD");
	printf ("    %s\n", _("Offset to result in critical status (seconds)"));
	printf (UT_TIMEOUT, DEFAULT_SOCKET_TIMEOUT);
	printf (UT_VERBOSE);

	printf("\n");
	printf("%s\n", _("This plugin checks the clock offset between the local host and a"));
	printf("%s\n", _("remote NTP server. It is independent of any commandline programs or"));
	printf("%s\n", _("external libraries."));

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("If you'd rather want to monitor an NTP server, please use"));
	printf(" %s\n", _("check_ntp_peer."));
	printf("\n");
	printf(UT_THRESHOLDS_NOTES);

	printf("\n");
	printf("%s\n", _("Examples:"));
	printf("  %s\n", ("./check_ntp_time -H ntpserv -w 0.5 -c 1"));

	printf (UT_SUPPORT);
}

void
print_usage(void)
{
	printf ("%s\n", _("Usage:"));
	printf(" %s -H <host> [-4|-6] [-w <warn>] [-c <crit>] [-v verbose]\n", progname);
}

