/******************************************************************************
 check_ntp.c: utility to check ntp servers independant of any commandline
              programs or external libraries.
 original author: sean finney <seanius@seanius.net>
 ******************************************************************************
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 $Id$
 
*****************************************************************************/

const char *progname = "check_ntp";
const char *revision = "$Revision$";
const char *copyright = "2006";
const char *email = "nagiosplug-devel@lists.sourceforge.net";

#include "common.h"
#include "netutils.h"
#include "utils.h"

static char *server_address=NULL;
static int verbose=0;
static int zero_offset_bad=0;
static double owarn=0;
static double ocrit=0;
static short do_jitter=0;
static double jwarn=0;
static double jcrit=0;

int process_arguments (int, char **);
void print_help (void);
void print_usage (void);

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
/* bits 6,7,8 are the ntp mode */
#define MODE_MASK 0x07
#define MODE(x) (x&MODE_MASK)
#define MODE_SET(x,y)	do{ x |= (y&MODE_MASK); }while(0)
/* here are some values */
#define MODE_CLIENT 0x03

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
			R32(n)=htonl((4294.967296*t.tv_usec)+.5); \
		} \
	} while(0)


/* calculate the offset of the local clock */
static inline double calc_offset(const ntp_message *m, const struct timeval *t){
	double client_tx, peer_rx, peer_tx, client_rx, rtdelay;
	client_tx = NTP64asDOUBLE(m->origts);
	peer_rx = NTP64asDOUBLE(m->rxts);
	peer_tx = NTP64asDOUBLE(m->txts);
	client_rx=TVasDOUBLE((*t));
	rtdelay=NTP32asDOUBLE(m->rtdelay);
	return (.5*((peer_tx-client_rx)+(peer_rx-client_tx)))-rtdelay;
}

/* print out a ntp packet in human readable/debuggable format */
void print_packet(const ntp_message *p){
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
	p->precision=0xfa;
	L16(p->rtdelay)=htons(1);
	L16(p->rtdisp)=htons(1);

	gettimeofday(&t, NULL);
	TVtoNTP64(t,p->txts);
}

double offset_request(const char *host){
	int i=0, conn=-1;
	ntp_message req;
	double next_offset=0., avg_offset=0.;
	struct timeval recv_time;

	for(i=0; i<4; i++){
		setup_request(&req);
		my_udp_connect(server_address, 123, &conn);
		write(conn, &req, sizeof(ntp_message));
		read(conn, &req, sizeof(ntp_message));
		gettimeofday(&recv_time, NULL);
		/* if(verbose) print_packet(&req); */
		close(conn);
		next_offset=calc_offset(&req, &recv_time);
		if(verbose) printf("offset: %g\n", next_offset);
		avg_offset+=next_offset;
	}
	return avg_offset/4.;
}

/* not yet implemented yet */
double jitter_request(const char *host){
	return 0.;
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
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"zero-offset", no_argument, 0, 'O'},
		{"jwarn", required_argument, 0, 'j'},
		{"jcrit", required_argument, 0, 'k'},
		{"timeout", required_argument, 0, 't'},
		{"hostname", required_argument, 0, 'H'},
		{0, 0, 0, 0}
	};

	
	if (argc < 2)
		usage ("\n");

	while (1) {
		c = getopt_long (argc, argv, "Vhv46w:c:Oj:k:t:H:", longopts, &option);
		if (c == -1 || c == EOF || c == 1)
			break;

		switch (c) {
		case 'h':
			print_help();
			exit(STATE_OK);
			break;
		case 'V':
			print_revision(progname, revision);
			exit(STATE_OK);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'w':
			owarn = atof(optarg);
			break;
		case 'c':
			ocrit = atof(optarg);
			break;
		case 'j':
			do_jitter=1;
			jwarn = atof(optarg);
			break;
		case 'k':
			do_jitter=1;
			jcrit = atof(optarg);
			break;
		case 'H':
			if(is_host(optarg) == FALSE)
				usage2(_("Invalid hostname/address"), optarg);
			server_address = strdup(optarg);
			break;
		case 't':
			socket_timeout=atoi(optarg);
			break;
		case 'O':
			zero_offset_bad=1;
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
			usage2 (_("Unknown argument"), optarg);
			break;
		}
	}

	if (ocrit < owarn){
		usage4(_("Critical offset should be larger than warning offset"));
	}

	if (ocrit < owarn){
		usage4(_("Critical jitter should be larger than warning jitter"));
	}

	if(server_address == NULL){
		usage4(_("Hostname was not supplied"));
	}

	return 0;
}

int main(int argc, char *argv[]){
	int result = STATE_UNKNOWN;
	double offset=0, jitter=0;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	offset = offset_request(server_address);
	if(offset > ocrit){
		printf("NTP CRITICAL: ");
		result = STATE_CRITICAL;
	} else if(offset > owarn) {
		printf("NTP WARNING: ");
		result = STATE_WARNING;
	} else {
		printf("NTP OK: ");
		result = STATE_OK;
	}

	/* not implemented yet: */
	jitter=jitter_request(server_address);

	/* not implemented yet:
	if(do_jitter){
		if(jitter > jcrit){
			printf("NTP CRITICAL: ");
			result = STATE_CRITICAL;
		} else if(jitter > jwarn) {
			printf("NTP WARNING: ");
			result = STATE_WARNING;
		} else {
			printf("NTP OK: ");
			result = STATE_OK;
		}
	}
	*/

	printf("Offset %g secs|offset=%g\n", offset, offset);

	if(server_address!=NULL) free(server_address);
	return result;
}


void print_usage(void){
	printf("\
Usage: %s -H <host> [-O] [-w <warn>] [-c <crit>] [-j <warn>] [-k <crit>] [-v verbose]\
\n", progname);
}

void print_help(void){
	print_revision(progname, revision);

	printf ("Copyright (c) 1999 Ethan Galstad\n");
	printf (COPYRIGHT, copyright, email);

	print_usage();
	printf (_(UT_HELP_VRSN));
	printf (_(UT_HOST_PORT), 'p', "123");
	printf (_(UT_WARN_CRIT));
	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);
	printf (_(UT_VERBOSE));
	printf(_(UT_SUPPORT));
}
