/*****************************************************************************
*
* Monitoring check_icmp plugin
*
* License: GPL
* Copyright (c) 2005-2008 Monitoring Plugins Development Team
* Original Author : Andreas Ericsson <ae@op5.se>
*
* Description:
*
* This file contains the check_icmp plugin
*
* Relevant RFC's: 792 (ICMP), 791 (IP)
*
* This program was modeled somewhat after the check_icmp program,
* which was in turn a hack of fping (www.fping.org) but has been
* completely rewritten since to generate higher precision rta values,
* and support several different modes as well as setting ttl to control.
* redundant routes. The only remainders of fping is currently a few
* function names.
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

/* progname may change */
/* char *progname = "check_icmp"; */
char *progname;
const char *copyright = "2005-2008";
const char *email = "devel@monitoring-plugins.org";

/** Monitoring Plugins basic includes */
#include "../plugins/common.h"
#include "netutils.h"
#include "utils.h"

#if HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <float.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>


/** sometimes undefined system macros (quite a few, actually) **/
#ifndef MAXTTL
# define MAXTTL	255
#endif
#ifndef INADDR_NONE
# define INADDR_NONE (in_addr_t)(-1)
#endif

#ifndef SOL_IP
#define SOL_IP 0
#endif

/* we bundle these in one #ifndef, since they're all from BSD
 * Put individual #ifndef's around those that bother you */
#ifndef ICMP_UNREACH_NET_UNKNOWN
# define ICMP_UNREACH_NET_UNKNOWN 6
# define ICMP_UNREACH_HOST_UNKNOWN 7
# define ICMP_UNREACH_ISOLATED 8
# define ICMP_UNREACH_NET_PROHIB 9
# define ICMP_UNREACH_HOST_PROHIB 10
# define ICMP_UNREACH_TOSNET 11
# define ICMP_UNREACH_TOSHOST 12
#endif
/* tru64 has the ones above, but not these */
#ifndef ICMP_UNREACH_FILTER_PROHIB
# define ICMP_UNREACH_FILTER_PROHIB 13
# define ICMP_UNREACH_HOST_PRECEDENCE 14
# define ICMP_UNREACH_PRECEDENCE_CUTOFF 15
#endif

typedef unsigned short range_t;  /* type for get_range() -- unimplemented */

typedef struct rta_host {
	unsigned short id;           /* id in **table, and icmp pkts */
	char *name;                  /* arg used for adding this host */
	char *msg;                   /* icmp error message, if any */
	struct sockaddr_storage saddr_in;     /* the address of this host */
	struct sockaddr_storage error_addr;   /* stores address of error replies */
	unsigned long long time_waited; /* total time waited, in usecs */
	unsigned int icmp_sent, icmp_recv, icmp_lost; /* counters */
	unsigned char icmp_type, icmp_code; /* type and code from errors */
	unsigned short flags;        /* control/status flags */
	double rta;                  /* measured RTA */
	int rta_status;              // check result for RTA checks
	double rtmax;                /* max rtt */
	double rtmin;                /* min rtt */
	double jitter;               /* measured jitter */
	int jitter_status;           // check result for Jitter checks
	double jitter_max;           /* jitter rtt maximum */
	double jitter_min;           /* jitter rtt minimum */
	double EffectiveLatency;
	double mos;                  /* Mean opnion score */
	int mos_status;              // check result for MOS checks
	double  score;               /* score */
	int score_status;            // check result for score checks
	u_int last_tdiff;
	u_int last_icmp_seq;         /* Last ICMP_SEQ to check out of order pkts */
	unsigned char pl;            /* measured packet loss */
	int pl_status;               // check result for packet loss checks
	struct rta_host *next;       /* linked list */
	int order_status;            // check result for packet order checks
} rta_host;

#define FLAG_LOST_CAUSE 0x01  /* decidedly dead target. */

/* threshold structure. all values are maximum allowed, exclusive */
typedef struct threshold {
	unsigned char pl;			/* max allowed packet loss in percent */
	unsigned int rta;			/* roundtrip time average, microseconds */
	double jitter;	/* jitter time average, microseconds */
	double mos;			/* MOS */
	double score;		/* Score */
} threshold;

/* the data structure */
typedef struct icmp_ping_data {
	struct timeval stime;	/* timestamp (saved in protocol struct as well) */
	unsigned short ping_id;
} icmp_ping_data;

typedef union ip_hdr {
	struct ip ip;
	struct ip6_hdr ip6;
} ip_hdr;

typedef union icmp_packet {
	void *buf;
	struct icmp *icp;
	struct icmp6_hdr *icp6;
	u_short *cksum_in;
} icmp_packet;

/* the different modes of this program are as follows:
 * MODE_RTA: send all packets no matter what (mimic check_icmp and check_ping)
 * MODE_HOSTCHECK: Return immediately upon any sign of life
 *                 In addition, sends packets to ALL addresses assigned
 *                 to this host (as returned by gethostbyname() or
 *                 gethostbyaddr() and expects one host only to be checked at
 *                 a time.  Therefore, any packet response what so ever will
 *                 count as a sign of life, even when received outside
 *                 crit.rta limit. Do not misspell any additional IP's.
 * MODE_ALL:  Requires packets from ALL requested IP to return OK (default).
 * MODE_ICMP: implement something similar to check_icmp (MODE_RTA without
 *            tcp and udp args does this)
 */
#define MODE_RTA 0
#define MODE_HOSTCHECK 1
#define MODE_ALL 2
#define MODE_ICMP 3

enum enum_threshold_mode {
	const_rta_mode,
	const_packet_loss_mode,
	const_jitter_mode,
	const_mos_mode,
	const_score_mode
};

typedef enum enum_threshold_mode threshold_mode;

/* the different ping types we can do
 * TODO: investigate ARP ping as well */
#define HAVE_ICMP 1
#define HAVE_UDP 2
#define HAVE_TCP 4
#define HAVE_ARP 8

#define MIN_PING_DATA_SIZE sizeof(struct icmp_ping_data)
#define MAX_IP_PKT_SIZE 65536	/* (theoretical) max IP packet size */
#define IP_HDR_SIZE 20
#define MAX_PING_DATA (MAX_IP_PKT_SIZE - IP_HDR_SIZE - ICMP_MINLEN)
#define DEFAULT_PING_DATA_SIZE (MIN_PING_DATA_SIZE + 44)

/* various target states */
#define TSTATE_INACTIVE 0x01	/* don't ping this host anymore */
#define TSTATE_WAITING 0x02		/* unanswered packets on the wire */
#define TSTATE_ALIVE 0x04       /* target is alive (has answered something) */
#define TSTATE_UNREACH 0x08

/** prototypes **/
void print_help (void);
void print_usage (void);
static u_int get_timevar(const char *);
static u_int get_timevaldiff(struct timeval *, struct timeval *);
static in_addr_t get_ip_address(const char *);
static int wait_for_reply(int, u_int);
static int recvfrom_wto(int, void *, unsigned int, struct sockaddr *, u_int *, struct timeval*);
static int send_icmp_ping(int, struct rta_host *);
static int get_threshold(char *str, threshold *th);
static bool get_threshold2(char *str, size_t length, threshold *, threshold *, threshold_mode mode);
static bool parse_threshold2_helper(char *s, size_t length, threshold *thr, threshold_mode mode);
static void run_checks(void);
static void set_source_ip(char *);
static int add_target(char *);
static int add_target_ip(char *, struct sockaddr_storage *);
static int handle_random_icmp(unsigned char *, struct sockaddr_storage *);
static void parse_address(struct sockaddr_storage *, char *, int);
static unsigned short icmp_checksum(uint16_t *, size_t);
static void finish(int);
static void crash(const char *, ...);

/** external **/
extern int optind;
extern char *optarg;
extern char **environ;

/** global variables **/
static struct rta_host **table, *cursor, *list;

static threshold crit = {
	.pl = 80,
	.rta = 500000,
	.jitter = 0.0,
	.mos = 0.0,
	.score = 0.0
};
static threshold warn = {
	.pl = 40,
	.rta = 200000,
	.jitter = 0.0,
	.mos = 0.0,
	.score = 0.0
};

static int mode, protocols, sockets, debug = 0, timeout = 10;
static unsigned short icmp_data_size = DEFAULT_PING_DATA_SIZE;
static unsigned short icmp_pkt_size = DEFAULT_PING_DATA_SIZE + ICMP_MINLEN;

static unsigned int icmp_sent = 0, icmp_recv = 0, icmp_lost = 0, ttl = 0;
#define icmp_pkts_en_route (icmp_sent - (icmp_recv + icmp_lost))
static unsigned short targets_down = 0, targets = 0, packets = 0;
#define targets_alive (targets - targets_down)
static unsigned int retry_interval, pkt_interval, target_interval;
static int icmp_sock, tcp_sock, udp_sock, status = STATE_OK;
static pid_t pid;
static struct timezone tz;
static struct timeval prog_start;
static unsigned long long max_completion_time = 0;
static unsigned int warn_down = 1, crit_down = 1; /* host down threshold values */
static int min_hosts_alive = -1;
float pkt_backoff_factor = 1.5;
float target_backoff_factor = 1.5;
bool rta_mode=false;
bool pl_mode=false;
bool jitter_mode=false;
bool score_mode=false;
bool mos_mode=false;
bool order_mode=false;

/** code start **/
static void
crash(const char *fmt, ...)
{
	va_list ap;

	printf("%s: ", progname);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if(errno) printf(": %s", strerror(errno));
	puts("");

	exit(3);
}


static const char *
get_icmp_error_msg(unsigned char icmp_type, unsigned char icmp_code)
{
	const char *msg = "unreachable";

	if(debug > 1) printf("get_icmp_error_msg(%u, %u)\n", icmp_type, icmp_code);
	switch(icmp_type) {
	case ICMP_UNREACH:
		switch(icmp_code) {
		case ICMP_UNREACH_NET: msg = "Net unreachable"; break;
		case ICMP_UNREACH_HOST:	msg = "Host unreachable"; break;
		case ICMP_UNREACH_PROTOCOL: msg = "Protocol unreachable (firewall?)"; break;
		case ICMP_UNREACH_PORT: msg = "Port unreachable (firewall?)"; break;
		case ICMP_UNREACH_NEEDFRAG: msg = "Fragmentation needed"; break;
		case ICMP_UNREACH_SRCFAIL: msg = "Source route failed"; break;
		case ICMP_UNREACH_ISOLATED: msg = "Source host isolated"; break;
		case ICMP_UNREACH_NET_UNKNOWN: msg = "Unknown network"; break;
		case ICMP_UNREACH_HOST_UNKNOWN: msg = "Unknown host"; break;
		case ICMP_UNREACH_NET_PROHIB: msg = "Network denied (firewall?)"; break;
		case ICMP_UNREACH_HOST_PROHIB: msg = "Host denied (firewall?)"; break;
		case ICMP_UNREACH_TOSNET: msg = "Bad TOS for network (firewall?)"; break;
		case ICMP_UNREACH_TOSHOST: msg = "Bad TOS for host (firewall?)"; break;
		case ICMP_UNREACH_FILTER_PROHIB: msg = "Prohibited by filter (firewall)"; break;
		case ICMP_UNREACH_HOST_PRECEDENCE: msg = "Host precedence violation"; break;
		case ICMP_UNREACH_PRECEDENCE_CUTOFF: msg = "Precedence cutoff"; break;
		default: msg = "Invalid code"; break;
		}
		break;

	case ICMP_TIMXCEED:
		/* really 'out of reach', or non-existent host behind a router serving
		 * two different subnets */
		switch(icmp_code) {
		case ICMP_TIMXCEED_INTRANS: msg = "Time to live exceeded in transit"; break;
		case ICMP_TIMXCEED_REASS: msg = "Fragment reassembly time exceeded"; break;
		default: msg = "Invalid code"; break;
		}
		break;

	case ICMP_SOURCEQUENCH: msg = "Transmitting too fast"; break;
	case ICMP_REDIRECT: msg = "Redirect (change route)"; break;
	case ICMP_PARAMPROB: msg = "Bad IP header (required option absent)"; break;

		/* the following aren't error messages, so ignore */
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
	default: msg = ""; break;
	}

	return msg;
}

static int
handle_random_icmp(unsigned char *packet, struct sockaddr_storage *addr)
{
	struct icmp p, sent_icmp;
	struct rta_host *host = NULL;

	memcpy(&p, packet, sizeof(p));
	if(p.icmp_type == ICMP_ECHO && ntohs(p.icmp_id) == pid) {
		/* echo request from us to us (pinging localhost) */
		return 0;
	}

	if(debug) printf("handle_random_icmp(%p, %p)\n", (void *)&p, (void *)addr);

	/* only handle a few types, since others can't possibly be replies to
	 * us in a sane network (if it is anyway, it will be counted as lost
	 * at summary time, but not as quickly as a proper response */
	/* TIMXCEED can be an unreach from a router with multiple IP's which
	 * serves two different subnets on the same interface and a dead host
	 * on one net is pinged from the other. The router will respond to
	 * itself and thus set TTL=0 so as to not loop forever.  Even when
	 * TIMXCEED actually sends a proper icmp response we will have passed
	 * too many hops to have a hope of reaching it later, in which case it
	 * indicates overconfidence in the network, poor routing or both. */
	if(p.icmp_type != ICMP_UNREACH && p.icmp_type != ICMP_TIMXCEED &&
	   p.icmp_type != ICMP_SOURCEQUENCH && p.icmp_type != ICMP_PARAMPROB)
	{
		return 0;
	}

	/* might be for us. At least it holds the original package (according
	 * to RFC 792). If it isn't, just ignore it */
	memcpy(&sent_icmp, packet + 28, sizeof(sent_icmp));
	if(sent_icmp.icmp_type != ICMP_ECHO || ntohs(sent_icmp.icmp_id) != pid ||
	   ntohs(sent_icmp.icmp_seq) >= targets*packets)
	{
		if(debug) printf("Packet is no response to a packet we sent\n");
		return 0;
	}

	/* it is indeed a response for us */
	host = table[ntohs(sent_icmp.icmp_seq)/packets];
	if(debug) {
		char address[INET6_ADDRSTRLEN];
		parse_address(addr, address, sizeof(address));
		printf("Received \"%s\" from %s for ICMP ECHO sent to %s.\n",
			get_icmp_error_msg(p.icmp_type, p.icmp_code),
			address, host->name);
	}

	icmp_lost++;
	host->icmp_lost++;
	/* don't spend time on lost hosts any more */
	if(host->flags & FLAG_LOST_CAUSE) return 0;

	/* source quench means we're sending too fast, so increase the
	 * interval and mark this packet lost */
	if(p.icmp_type == ICMP_SOURCEQUENCH) {
		pkt_interval *= pkt_backoff_factor;
		target_interval *= target_backoff_factor;
	}
	else {
		targets_down++;
		host->flags |= FLAG_LOST_CAUSE;
	}
	host->icmp_type = p.icmp_type;
	host->icmp_code = p.icmp_code;
	host->error_addr = *addr;

	return 0;
}

void parse_address(struct sockaddr_storage *addr, char *address, int size)
{
	switch (address_family) {
	case AF_INET:
		inet_ntop(address_family, &((struct sockaddr_in *)addr)->sin_addr, address, size);
	break;
	case AF_INET6:
		inet_ntop(address_family, &((struct sockaddr_in6 *)addr)->sin6_addr, address, size);
	break;
	}
}

int
main(int argc, char **argv)
{
	int i;
	char *ptr;
	long int arg;
	int icmp_sockerrno, udp_sockerrno, tcp_sockerrno;
	int result;
	struct rta_host *host;
#ifdef HAVE_SIGACTION
	struct sigaction sig_action;
#endif
#ifdef SO_TIMESTAMP
	int on = 1;
#endif
	char *source_ip = NULL;
	char * opts_str = "vhVw:c:n:p:t:H:s:i:b:I:l:m:P:R:J:S:M:O64";
	setlocale (LC_ALL, "");
	bindtextdomain (PACKAGE, LOCALEDIR);
	textdomain (PACKAGE);

	/* we only need to be setsuid when we get the sockets, so do
	 * that before pointer magic (esp. on network data) */
	icmp_sockerrno = udp_sockerrno = tcp_sockerrno = sockets = 0;

	address_family = -1;
	int icmp_proto = IPPROTO_ICMP;

	/* get calling name the old-fashioned way for portability instead
	 * of relying on the glibc-ism __progname */
	ptr = strrchr(argv[0], '/');
	if(ptr) progname = &ptr[1];
	else progname = argv[0];

	/* now set defaults. Use progname to set them initially (allows for
	 * superfast check_host program when target host is up */
	cursor = list = NULL;
	table = NULL;

	mode = MODE_RTA;
	/* Default critical thresholds */
	crit.rta = 500000;
	crit.pl = 80;
	crit.jitter = 50;
	crit.mos= 3;
	crit.score=70;
	/* Default warning thresholds */
	warn.rta = 200000;
	warn.pl = 40;
	warn.jitter = 40;
	warn.mos= 3.5;
	warn.score=80;

	protocols = HAVE_ICMP | HAVE_UDP | HAVE_TCP;
	pkt_interval = 80000;  /* 80 msec packet interval by default */
	packets = 5;

	if(!strcmp(progname, "check_icmp") || !strcmp(progname, "check_ping")) {
		mode = MODE_ICMP;
		protocols = HAVE_ICMP;
	}
	else if(!strcmp(progname, "check_host")) {
		mode = MODE_HOSTCHECK;
		pkt_interval = 1000000;
		packets = 5;
		crit.rta = warn.rta = 1000000;
		crit.pl = warn.pl = 100;
	}
	else if(!strcmp(progname, "check_rta_multi")) {
		mode = MODE_ALL;
		target_interval = 0;
		pkt_interval = 50000;
		packets = 5;
	}

	/* support "--help" and "--version" */
	if(argc == 2) {
		if(!strcmp(argv[1], "--help"))
			strcpy(argv[1], "-h");
		if(!strcmp(argv[1], "--version"))
			strcpy(argv[1], "-V");
	}

	/* Parse protocol arguments first */
	for(i = 1; i < argc; i++) {
		while((arg = getopt(argc, argv, opts_str)) != EOF) {
			switch(arg) {
			case '4':
				if (address_family != -1)
					crash("Multiple protocol versions not supported");
				address_family = AF_INET;
				break;
			case '6':
#ifdef USE_IPV6
				if (address_family != -1)
					crash("Multiple protocol versions not supported");
				address_family = AF_INET6;
#else
				usage (_("IPv6 support not available\n"));
#endif
				break;
			}
		}
	}

	/* Reset argument scanning */
	optind = 1;

	unsigned long size;
	bool err;
	/* parse the arguments */
	for(i = 1; i < argc; i++) {
		while((arg = getopt(argc, argv, opts_str)) != EOF) {
			switch(arg) {
			case 'v':
				debug++;
				break;
			case 'b':
				size = strtol(optarg,NULL,0);
				if (size >= (sizeof(struct icmp) + sizeof(struct icmp_ping_data)) &&
				    size < MAX_PING_DATA) {
					icmp_data_size = size;
					icmp_pkt_size = size + ICMP_MINLEN;
				} else
					usage_va("ICMP data length must be between: %lu and %lu",
					         sizeof(struct icmp) + sizeof(struct icmp_ping_data),
					         MAX_PING_DATA - 1);
				break;
			case 'i':
				pkt_interval = get_timevar(optarg);
				break;
			case 'I':
				target_interval = get_timevar(optarg);
				break;
			case 'w':
				get_threshold(optarg, &warn);
				break;
			case 'c':
				get_threshold(optarg, &crit);
				break;
			case 'n':
			case 'p':
				packets = strtoul(optarg, NULL, 0);
				break;
			case 't':
				timeout = strtoul(optarg, NULL, 0);
				if(!timeout) timeout = 10;
				break;
			case 'H':
				add_target(optarg);
				break;
			case 'l':
				ttl = (int)strtoul(optarg, NULL, 0);
				break;
			case 'm':
				min_hosts_alive = (int)strtoul(optarg, NULL, 0);
				break;
			case 'd': /* implement later, for cluster checks */
				warn_down = (unsigned char)strtoul(optarg, &ptr, 0);
				if(ptr) {
					crit_down = (unsigned char)strtoul(ptr + 1, NULL, 0);
				}
				break;
			case 's': /* specify source IP address */
				source_ip = optarg;
				break;
			case 'V': /* version */
				print_revision (progname, NP_VERSION);
				exit (STATE_UNKNOWN);
			case 'h': /* help */
				print_help ();
				exit (STATE_UNKNOWN);
				break;
			case 'R': /* RTA mode */
				err = get_threshold2(optarg, strlen(optarg), &warn, &crit, const_rta_mode);
				if (!err) {
						crash("Failed to parse RTA threshold");
				}

				rta_mode=true;
				break;
			case 'P': /* packet loss mode */
				err = get_threshold2(optarg, strlen(optarg), &warn, &crit, const_packet_loss_mode);
				if (!err) {
						crash("Failed to parse packet loss threshold");
				}

				pl_mode=true;
				break;
			case 'J': /* jitter mode */
				err = get_threshold2(optarg, strlen(optarg), &warn, &crit, const_jitter_mode);
				if (!err) {
						crash("Failed to parse jitter threshold");
				}

				jitter_mode=true;
				break;
			case 'M': /* MOS mode */
				err = get_threshold2(optarg, strlen(optarg), &warn, &crit, const_mos_mode);
				if (!err) {
						crash("Failed to parse MOS threshold");
				}

				mos_mode=true;
				break;
			case 'S': /* score mode */
				err = get_threshold2(optarg, strlen(optarg), &warn, &crit, const_score_mode);
				if (!err) {
						crash("Failed to parse score threshold");
				}

				score_mode=true;
				break;
			case 'O': /* out of order mode */
				order_mode=true;
				break;
			}
		}
	}

	/* POSIXLY_CORRECT might break things, so unset it (the portable way) */
	environ = NULL;

	/* use the pid to mark packets as ours */
	/* Some systems have 32-bit pid_t so mask off only 16 bits */
	pid = getpid() & 0xffff;
	/* printf("pid = %u\n", pid); */

	/* Parse extra opts if any */
	argv=np_extra_opts(&argc, argv, progname);

	argv = &argv[optind];
	while(*argv) {
		add_target(*argv);
		argv++;
	}

	if(!targets) {
		errno = 0;
		crash("No hosts to check");
	}

	// add_target might change address_family
	switch ( address_family ){
		case AF_INET:	icmp_proto = IPPROTO_ICMP;
				break;
		case AF_INET6:	icmp_proto = IPPROTO_ICMPV6;
				break;
		default:	crash("Address family not supported");
	}
	if((icmp_sock = socket(address_family, SOCK_RAW, icmp_proto)) != -1)
		sockets |= HAVE_ICMP;
	else icmp_sockerrno = errno;

	if( source_ip )
		set_source_ip(source_ip);

#ifdef SO_TIMESTAMP
	if(setsockopt(icmp_sock, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on)))
	  if(debug) printf("Warning: no SO_TIMESTAMP support\n");
#endif // SO_TIMESTAMP

	/* now drop privileges (no effect if not setsuid or geteuid() == 0) */
	if (setuid(getuid()) == -1) {
		printf("ERROR: Failed to drop privileges\n");
		return 1;
	}

	if(!sockets) {
		if(icmp_sock == -1) {
			errno = icmp_sockerrno;
			crash("Failed to obtain ICMP socket");
			return -1;
		}
		/* if(udp_sock == -1) { */
		/* 	errno = icmp_sockerrno; */
		/* 	crash("Failed to obtain UDP socket"); */
		/* 	return -1; */
		/* } */
		/* if(tcp_sock == -1) { */
		/* 	errno = icmp_sockerrno; */
		/* 	crash("Failed to obtain TCP socker"); */
		/* 	return -1; */
		/* } */
	}
	if(!ttl) ttl = 64;

	if(icmp_sock) {
		result = setsockopt(icmp_sock, SOL_IP, IP_TTL, &ttl, sizeof(ttl));
		if(debug) {
			if(result == -1) printf("setsockopt failed\n");
			else printf("ttl set to %u\n", ttl);
		}
	}

	/* stupid users should be able to give whatever thresholds they want
	 * (nothing will break if they do), but some anal plugin maintainer
	 * will probably add some printf() thing here later, so it might be
	 * best to at least show them where to do it. ;) */
	if(warn.pl > crit.pl) warn.pl = crit.pl;
	if(warn.rta > crit.rta) warn.rta = crit.rta;
	if(warn_down > crit_down) crit_down = warn_down;
	if(warn.jitter > crit.jitter) crit.jitter = warn.jitter;
	if(warn.mos < crit.mos) warn.mos = crit.mos;
	if(warn.score < crit.score) warn.score = crit.score;

#ifdef HAVE_SIGACTION
	sig_action.sa_sigaction = NULL;
	sig_action.sa_handler = finish;
	sigfillset(&sig_action.sa_mask);
	sig_action.sa_flags = SA_NODEFER|SA_RESTART;
	sigaction(SIGINT, &sig_action, NULL);
	sigaction(SIGHUP, &sig_action, NULL);
	sigaction(SIGTERM, &sig_action, NULL);
	sigaction(SIGALRM, &sig_action, NULL);
#else /* HAVE_SIGACTION */
	signal(SIGINT, finish);
	signal(SIGHUP, finish);
	signal(SIGTERM, finish);
	signal(SIGALRM, finish);
#endif /* HAVE_SIGACTION */
	if(debug) printf("Setting alarm timeout to %u seconds\n", timeout);
	alarm(timeout);

	/* make sure we don't wait any longer than necessary */
	gettimeofday(&prog_start, &tz);
	max_completion_time =
		((targets * packets * pkt_interval) + (targets * target_interval)) +
		(targets * packets * crit.rta) + crit.rta;

	if(debug) {
		printf("packets: %u, targets: %u\n"
			   "target_interval: %0.3f, pkt_interval %0.3f\n"
			   "crit.rta: %0.3f\n"
			   "max_completion_time: %0.3f\n",
			   packets, targets,
			   (float)target_interval / 1000, (float)pkt_interval / 1000,
			   (float)crit.rta / 1000,
			   (float)max_completion_time / 1000);
	}

	if(debug) {
		if(max_completion_time > (u_int)timeout * 1000000) {
			printf("max_completion_time: %llu  timeout: %u\n",
				   max_completion_time, timeout);
			printf("Timeout must be at least %llu\n",
				   max_completion_time / 1000000 + 1);
		}
	}

	if(debug) {
		printf("crit = {%u, %u%%}, warn = {%u, %u%%}\n",
			   crit.rta, crit.pl, warn.rta, warn.pl);
		printf("pkt_interval: %u  target_interval: %u  retry_interval: %u\n",
			   pkt_interval, target_interval, retry_interval);
		printf("icmp_pkt_size: %u  timeout: %u\n",
			   icmp_pkt_size, timeout);
	}

	if(packets > 20) {
		errno = 0;
		crash("packets is > 20 (%d)", packets);
	}

	if(min_hosts_alive < -1) {
		errno = 0;
		crash("minimum alive hosts is negative (%i)", min_hosts_alive);
	}

	host = list;
	table = malloc(sizeof(struct rta_host *) * targets);
	if(!table) {
		crash("main(): malloc failed for host table");
	}

	i = 0;
	while(host) {
		host->id = i*packets;
		table[i] = host;
		host = host->next;
		i++;
	}

	run_checks();

	errno = 0;
	finish(0);

	return(0);
}

static void
run_checks()
{
	u_int i, t;
	u_int final_wait, time_passed;

	/* this loop might actually violate the pkt_interval or target_interval
	 * settings, but only if there aren't any packets on the wire which
	 * indicates that the target can handle an increased packet rate */
	for(i = 0; i < packets; i++) {
		for(t = 0; t < targets; t++) {
			/* don't send useless packets */
			if(!targets_alive) finish(0);
			if(table[t]->flags & FLAG_LOST_CAUSE) {
				if(debug) printf("%s is a lost cause. not sending any more\n",
								 table[t]->name);
				continue;
			}

			/* we're still in the game, so send next packet */
			(void)send_icmp_ping(icmp_sock, table[t]);
			wait_for_reply(icmp_sock, target_interval);
		}
		wait_for_reply(icmp_sock, pkt_interval * targets);
	}

	if(icmp_pkts_en_route && targets_alive) {
		time_passed = get_timevaldiff(NULL, NULL);
		final_wait = max_completion_time - time_passed;

		if(debug) {
			printf("time_passed: %u  final_wait: %u  max_completion_time: %llu\n",
				   time_passed, final_wait, max_completion_time);
		}
		if(time_passed > max_completion_time) {
			if(debug) printf("Time passed. Finishing up\n");
			finish(0);
		}

		/* catch the packets that might come in within the timeframe, but
		 * haven't yet */
		if(debug) printf("Waiting for %u micro-seconds (%0.3f msecs)\n",
						 final_wait, (float)final_wait / 1000);
		wait_for_reply(icmp_sock, final_wait);
	}
}


/* response structure:
 * IPv4:
 * ip header   : 20 bytes
 * icmp header : 28 bytes
 * IPv6:
 * ip header   : 40 bytes
 * icmp header : 28 bytes
 * both:
 * icmp echo reply : the rest
 */
static int
wait_for_reply(int sock, u_int t)
{
	int n, hlen;
	static unsigned char buf[65536];
	struct sockaddr_storage resp_addr;
	union ip_hdr *ip;
	union icmp_packet packet;
	struct rta_host *host;
	struct icmp_ping_data data;
	struct timeval wait_start, now;
	u_int tdiff, i, per_pkt_wait;
	double jitter_tmp;

	if (!(packet.buf = malloc(icmp_pkt_size))) {
		crash("send_icmp_ping(): failed to malloc %d bytes for send buffer",
			icmp_pkt_size);
		return -1;	/* might be reached if we're in debug mode */
	}

	memset(packet.buf, 0, icmp_pkt_size);

	/* if we can't listen or don't have anything to listen to, just return */
	if(!t || !icmp_pkts_en_route) {
		free(packet.buf);
		return 0;
	}

	gettimeofday(&wait_start, &tz);

	i = t;
	per_pkt_wait = t / icmp_pkts_en_route;
	while(icmp_pkts_en_route && get_timevaldiff(&wait_start, NULL) < i) {
		t = per_pkt_wait;

		/* wrap up if all targets are declared dead */
		if(!targets_alive ||
		   get_timevaldiff(&prog_start, NULL) >= max_completion_time ||
		   (mode == MODE_HOSTCHECK && targets_down))
		{
			finish(0);
		}

		/* reap responses until we hit a timeout */
		n = recvfrom_wto(sock, buf, sizeof(buf),
					 (struct sockaddr *)&resp_addr, &t, &now);
		if(!n) {
			if(debug > 1) {
				printf("recvfrom_wto() timed out during a %u usecs wait\n",
					   per_pkt_wait);
			}
			continue;	/* timeout for this one, so keep trying */
		}
		if(n < 0) {
			if(debug) printf("recvfrom_wto() returned errors\n");
			free(packet.buf);
			return n;
		}

		// FIXME: with ipv6 we don't have an ip header here
		if (address_family != AF_INET6) {
			ip = (union ip_hdr *)buf;

			if(debug > 1) {
				char address[INET6_ADDRSTRLEN];
				parse_address(&resp_addr, address, sizeof(address));
				printf("received %u bytes from %s\n",
					address_family == AF_INET6 ? ntohs(ip->ip6.ip6_plen)
								   : ntohs(ip->ip.ip_len),
					address);
			}
		}

/* obsolete. alpha on tru64 provides the necessary defines, but isn't broken */
/* #if defined( __alpha__ ) && __STDC__ && !defined( __GLIBC__ ) */
		/* alpha headers are decidedly broken. Using an ansi compiler,
		 * they provide ip_vhl instead of ip_hl and ip_v, so we mask
		 * off the bottom 4 bits */
/* 		hlen = (ip->ip_vhl & 0x0f) << 2; */
/* #else */
		hlen = (address_family == AF_INET6) ? 0 : ip->ip.ip_hl << 2;
/* #endif */

		if(n < (hlen + ICMP_MINLEN)) {
			char address[INET6_ADDRSTRLEN];
			parse_address(&resp_addr, address, sizeof(address));
			crash("received packet too short for ICMP (%d bytes, expected %d) from %s\n",
				  n, hlen + icmp_pkt_size, address);
		}
		/* else if(debug) { */
		/* 	printf("ip header size: %u, packet size: %u (expected %u, %u)\n", */
		/* 		   hlen, ntohs(ip->ip_len) - hlen, */
		/* 		   sizeof(struct ip), icmp_pkt_size); */
		/* } */

		/* check the response */

		memcpy(packet.buf, buf + hlen, icmp_pkt_size);
/*			address_family == AF_INET6 ? sizeof(struct icmp6_hdr)
						   : sizeof(struct icmp));*/

		if(   (address_family == PF_INET &&
			(ntohs(packet.icp->icmp_id) != pid || packet.icp->icmp_type != ICMP_ECHOREPLY
			 || ntohs(packet.icp->icmp_seq) >= targets * packets))
		   || (address_family == PF_INET6 &&
			(ntohs(packet.icp6->icmp6_id) != pid || packet.icp6->icmp6_type != ICMP6_ECHO_REPLY
			|| ntohs(packet.icp6->icmp6_seq) >= targets * packets))) {
			if(debug > 2) printf("not a proper ICMP_ECHOREPLY\n");
			handle_random_icmp(buf + hlen, &resp_addr);
			continue;
		}

		/* this is indeed a valid response */
		if (address_family == PF_INET) {
			memcpy(&data, packet.icp->icmp_data, sizeof(data));
			if (debug > 2)
				printf("ICMP echo-reply of len %lu, id %u, seq %u, cksum 0x%X\n",
					(unsigned long)sizeof(data), ntohs(packet.icp->icmp_id),
					ntohs(packet.icp->icmp_seq), packet.icp->icmp_cksum);
			host = table[ntohs(packet.icp->icmp_seq)/packets];
		} else {
			memcpy(&data, &packet.icp6->icmp6_dataun.icmp6_un_data8[4], sizeof(data));
			if (debug > 2)
				printf("ICMP echo-reply of len %lu, id %u, seq %u, cksum 0x%X\n",
					(unsigned long)sizeof(data), ntohs(packet.icp6->icmp6_id),
					ntohs(packet.icp6->icmp6_seq), packet.icp6->icmp6_cksum);
			host = table[ntohs(packet.icp6->icmp6_seq)/packets];
		}

		tdiff = get_timevaldiff(&data.stime, &now);

		if (host->last_tdiff>0) {
			/* Calculate jitter */
			if (host->last_tdiff > tdiff) {
				jitter_tmp = host->last_tdiff - tdiff;
			} else {
				jitter_tmp = tdiff - host->last_tdiff;
			}

			if (host->jitter==0) {
				host->jitter=jitter_tmp;
				host->jitter_max=jitter_tmp;
				host->jitter_min=jitter_tmp;
			} else {
				host->jitter+=jitter_tmp;

				if (jitter_tmp < host->jitter_min) {
					host->jitter_min=jitter_tmp;
				}

				if (jitter_tmp > host->jitter_max) {
					host->jitter_max=jitter_tmp;
				}
			}

			/* Check if packets in order */
			if (host->last_icmp_seq >= packet.icp->icmp_seq)
				host->order_status=STATE_CRITICAL;
		}
		host->last_tdiff=tdiff;

		host->last_icmp_seq=packet.icp->icmp_seq;

		host->time_waited += tdiff;
		host->icmp_recv++;
		icmp_recv++;
		if (tdiff > (unsigned int)host->rtmax)
			host->rtmax = tdiff;
		if (tdiff < (unsigned int)host->rtmin)
			host->rtmin = tdiff;

		if(debug) {
			char address[INET6_ADDRSTRLEN];
			parse_address(&resp_addr, address, sizeof(address));

			switch(address_family) {
				case AF_INET: {
					printf("%0.3f ms rtt from %s, outgoing ttl: %u, incoming ttl: %u, max: %0.3f, min: %0.3f\n",
						(float)tdiff / 1000,
						address,
						ttl,
						ip->ip.ip_ttl,
						(float)host->rtmax / 1000,
						(float)host->rtmin / 1000);
					break;
					  };
				case AF_INET6: {
					printf("%0.3f ms rtt from %s, outgoing ttl: %u, max: %0.3f, min: %0.3f\n",
						(float)tdiff / 1000,
						address,
						ttl,
						(float)host->rtmax / 1000,
						(float)host->rtmin / 1000);
					  };
			   }
		}

		/* if we're in hostcheck mode, exit with limited printouts */
		if(mode == MODE_HOSTCHECK) {
			printf("OK - %s responds to ICMP. Packet %u, rta %0.3fms|"
				"pkt=%u;;;0;%u rta=%0.3f;%0.3f;%0.3f;;\n",
				host->name, icmp_recv, (float)tdiff / 1000,
				icmp_recv, packets, (float)tdiff / 1000,
				(float)warn.rta / 1000, (float)crit.rta / 1000);
			exit(STATE_OK);
		}
	}

	free(packet.buf);
	return 0;
}

/* the ping functions */
static int
send_icmp_ping(int sock, struct rta_host *host)
{
	long int len;
	size_t addrlen;
	struct icmp_ping_data data;
	struct msghdr hdr;
	struct iovec iov;
	struct timeval tv;
	void *buf = NULL;

	if(sock == -1) {
		errno = 0;
		crash("Attempt to send on bogus socket");
		return -1;
	}

	if(!buf) {
		if (!(buf = malloc(icmp_pkt_size))) {
			crash("send_icmp_ping(): failed to malloc %d bytes for send buffer",
				  icmp_pkt_size);
			return -1;	/* might be reached if we're in debug mode */
		}
	}
	memset(buf, 0, icmp_pkt_size);

	if((gettimeofday(&tv, &tz)) == -1) {
		free(buf);
		return -1;
	}

	data.ping_id = 10; /* host->icmp.icmp_sent; */
	memcpy(&data.stime, &tv, sizeof(tv));

	if (address_family == AF_INET) {
		struct icmp *icp = (struct icmp*)buf;
		addrlen = sizeof(struct sockaddr_in);

		memcpy(&icp->icmp_data, &data, sizeof(data));

		icp->icmp_type = ICMP_ECHO;
		icp->icmp_code = 0;
		icp->icmp_cksum = 0;
		icp->icmp_id = htons(pid);
		icp->icmp_seq = htons(host->id++);
		icp->icmp_cksum = icmp_checksum((uint16_t*)buf, (size_t)icmp_pkt_size);

		if (debug > 2)
			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to host %s\n",
				(unsigned long)sizeof(data), ntohs(icp->icmp_id), ntohs(icp->icmp_seq), icp->icmp_cksum, host->name);
	}
	else {
		struct icmp6_hdr *icp6 = (struct icmp6_hdr*)buf;
		addrlen = sizeof(struct sockaddr_in6);

		memcpy(&icp6->icmp6_dataun.icmp6_un_data8[4], &data, sizeof(data));

		icp6->icmp6_type = ICMP6_ECHO_REQUEST;
		icp6->icmp6_code = 0;
		icp6->icmp6_cksum = 0;
		icp6->icmp6_id = htons(pid);
		icp6->icmp6_seq = htons(host->id++);
		// let checksum be calculated automatically

		if (debug > 2) {
			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to host %s\n",
				(unsigned long)sizeof(data), ntohs(icp6->icmp6_id),
				ntohs(icp6->icmp6_seq), icp6->icmp6_cksum, host->name);
		}
	}

	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = icmp_pkt_size;

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = (struct sockaddr *)&host->saddr_in;
	hdr.msg_namelen = addrlen;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;

	errno = 0;

/* MSG_CONFIRM is a linux thing and only available on linux kernels >= 2.3.15, see send(2) */
#ifdef MSG_CONFIRM
	len = sendmsg(sock, &hdr, MSG_CONFIRM);
#else
	len = sendmsg(sock, &hdr, 0);
#endif

	free(buf);

	if(len < 0 || (unsigned int)len != icmp_pkt_size) {
		if(debug) {
			char address[INET6_ADDRSTRLEN];
			parse_address((struct sockaddr_storage *)&host->saddr_in, address, sizeof(address));
			printf("Failed to send ping to %s: %s\n", address, strerror(errno));
		}
		errno = 0;
		return -1;
	}

	icmp_sent++;
	host->icmp_sent++;

	return 0;
}

static int
recvfrom_wto(int sock, void *buf, unsigned int len, struct sockaddr *saddr,
			 u_int *timo, struct timeval* tv)
{
	u_int slen;
	int n, ret;
	struct timeval to, then, now;
	fd_set rd, wr;
#ifdef HAVE_MSGHDR_MSG_CONTROL
	char ans_data[4096];
#endif // HAVE_MSGHDR_MSG_CONTROL
	struct msghdr hdr;
	struct iovec iov;
#ifdef SO_TIMESTAMP
	struct cmsghdr* chdr;
#endif

	if(!*timo) {
		if(debug) printf("*timo is not\n");
		return 0;
	}

	to.tv_sec = *timo / 1000000;
	to.tv_usec = (*timo - (to.tv_sec * 1000000));

	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(sock, &rd);
	errno = 0;
	gettimeofday(&then, &tz);
	n = select(sock + 1, &rd, &wr, NULL, &to);
	if(n < 0) crash("select() in recvfrom_wto");
	gettimeofday(&now, &tz);
	*timo = get_timevaldiff(&then, &now);

	if(!n) return 0;				/* timeout */

	slen = sizeof(struct sockaddr_storage);

	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = len;

	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = saddr;
	hdr.msg_namelen = slen;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;
#ifdef HAVE_MSGHDR_MSG_CONTROL
	hdr.msg_control = ans_data;
	hdr.msg_controllen = sizeof(ans_data);
#endif

	ret = recvmsg(sock, &hdr, 0);
#ifdef SO_TIMESTAMP
	for(chdr = CMSG_FIRSTHDR(&hdr); chdr; chdr = CMSG_NXTHDR(&hdr, chdr)) {
		if(chdr->cmsg_level == SOL_SOCKET
		   && chdr->cmsg_type == SO_TIMESTAMP
		   && chdr->cmsg_len >= CMSG_LEN(sizeof(struct timeval))) {
			memcpy(tv, CMSG_DATA(chdr), sizeof(*tv));
			break ;
		}
	}

	if (!chdr)
#endif // SO_TIMESTAMP
		gettimeofday(tv, &tz);
	return (ret);
}

static void
finish(int sig)
{
	u_int i = 0;
	unsigned char pl;
	double rta;
	struct rta_host *host;
	const char *status_string[] =
	{"OK", "WARNING", "CRITICAL", "UNKNOWN", "DEPENDENT"};
	int hosts_ok = 0;
	int hosts_warn = 0;
	int this_status;
	double R;

	alarm(0);
	if(debug > 1) printf("finish(%d) called\n", sig);

	if(icmp_sock != -1) close(icmp_sock);
	if(udp_sock != -1) close(udp_sock);
	if(tcp_sock != -1) close(tcp_sock);

	if(debug) {
		printf("icmp_sent: %u  icmp_recv: %u  icmp_lost: %u\n",
			   icmp_sent, icmp_recv, icmp_lost);
		printf("targets: %u  targets_alive: %u\n", targets, targets_alive);
	}

	/* iterate thrice to calculate values, give output, and print perfparse */
	status=STATE_OK;
	host = list;

	while(host) {
		this_status = STATE_OK;

		if(!host->icmp_recv) {
			/* rta 0 is ofcourse not entirely correct, but will still show up
			 * conspicuously as missing entries in perfparse and cacti */
			pl = 100;
			rta = 0;
			status = STATE_CRITICAL;
			/* up the down counter if not already counted */
			if(!(host->flags & FLAG_LOST_CAUSE) && targets_alive) targets_down++;
		} else {
			pl = ((host->icmp_sent - host->icmp_recv) * 100) / host->icmp_sent;
			rta = (double)host->time_waited / host->icmp_recv;
		}

		if (host->icmp_recv>1) {
			/*
			 * This algorithm is probably pretty much blindly copied from
			 * locations like this one: https://www.slac.stanford.edu/comp/net/wan-mon/tutorial.html#mos
			 * It calculates a MOS value (range of 1 to 5, where 1 is bad and 5 really good).
			 * According to some quick research MOS originates from the Audio/Video transport network area.
			 * Whether it can and should be computed from ICMP data, I can not say.
			 *
			 * Anyway the basic idea is to map a value "R" with a range of 0-100 to the MOS value
			 *
			 * MOS stands likely for Mean Opinion Score ( https://en.wikipedia.org/wiki/Mean_Opinion_Score )
			 *
			 * More links:
			 * - https://confluence.slac.stanford.edu/display/IEPM/MOS
			 */
			host->jitter=(host->jitter / (host->icmp_recv - 1)/1000);

			/*
			 * Take the average round trip latency (in milliseconds), add
			 * round trip jitter, but double the impact to latency
			 * then add 10 for protocol latencies (in milliseconds).
			 */
			host->EffectiveLatency = (rta/1000) + host->jitter * 2 + 10;

			if (host->EffectiveLatency < 160) {
			   R = 93.2 - (host->EffectiveLatency / 40);
			} else {
			   R = 93.2 - ((host->EffectiveLatency - 120) / 10);
			}

			// Now, let us deduct 2.5 R values per percentage of packet loss (i.e. a
			// loss of 5% will be entered as 5).
			R = R - (pl * 2.5);

			if (R < 0) {
				R = 0;
			}

			host->score = R;
			host->mos= 1 + ((0.035) * R) + ((.000007) * R * (R-60) * (100-R));
		} else {
			host->jitter=0;
			host->jitter_min=0;
			host->jitter_max=0;
			host->mos=0;
		}

		host->pl = pl;
		host->rta = rta;

		/* if no new mode selected, use old schema */
		if (!rta_mode && !pl_mode && !jitter_mode && !score_mode && !mos_mode && !order_mode) {
			rta_mode = true;
			pl_mode = true;
		}

		/* Check which mode is on and do the warn / Crit stuff */
		if (rta_mode) {
			if(rta >= crit.rta) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->rta_status=STATE_CRITICAL;
			} else if(status!=STATE_CRITICAL && (rta >= warn.rta)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->rta_status=STATE_WARNING;
			}
		}

		if (pl_mode) {
			if(pl >= crit.pl) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->pl_status=STATE_CRITICAL;
			} else if(status!=STATE_CRITICAL && (pl >= warn.pl)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->pl_status=STATE_WARNING;
			}
		}

		if (jitter_mode) {
			if(host->jitter >= crit.jitter) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->jitter_status=STATE_CRITICAL;
			} else if(status!=STATE_CRITICAL && (host->jitter >= warn.jitter)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->jitter_status=STATE_WARNING;
			}
		}

		if (mos_mode) {
			if(host->mos <= crit.mos) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->mos_status=STATE_CRITICAL;
			} else if(status!=STATE_CRITICAL && (host->mos <= warn.mos)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->mos_status=STATE_WARNING;
			}
		}

		if (score_mode) {
			if(host->score <= crit.score) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->score_status=STATE_CRITICAL;
			} else if(status!=STATE_CRITICAL && (host->score <= warn.score)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->score_status=STATE_WARNING;
			}
		}

		if (this_status == STATE_WARNING) {
			hosts_warn++;
		} else if (this_status == STATE_OK) {
			hosts_ok++;
		}

		host = host->next;
	}


	/* this is inevitable */
	if(!targets_alive) status = STATE_CRITICAL;
	if(min_hosts_alive > -1) {
		if(hosts_ok >= min_hosts_alive) status = STATE_OK;
		else if((hosts_ok + hosts_warn) >= min_hosts_alive) status = STATE_WARNING;
	}
	printf("%s - ", status_string[status]);

	host = list;
	while(host) {

		if(debug) puts("");
		if(i) {
			if(i < targets) printf(" :: ");
			else printf("\n");
		}
		i++;
		if(!host->icmp_recv) {
			status = STATE_CRITICAL;
			host->rtmin=0;
			host->jitter_min=0;
			if(host->flags & FLAG_LOST_CAUSE) {
				char address[INET6_ADDRSTRLEN];
				parse_address(&host->error_addr, address, sizeof(address));
				printf("%s: %s @ %s. rta nan, lost %d%%",
					   host->name,
					   get_icmp_error_msg(host->icmp_type, host->icmp_code),
					   address,
					   100);
			} else { /* not marked as lost cause, so we have no flags for it */
				printf("%s: rta nan, lost 100%%", host->name);
			}
		} else {	/* !icmp_recv */
			printf("%s", host->name);
			/* rta text output */
			if (rta_mode) {
				if (status == STATE_OK)
					printf(" rta %0.3fms", host->rta / 1000);
				else if (status==STATE_WARNING && host->rta_status==status)
					printf(" rta %0.3fms > %0.3fms", (float)host->rta / 1000, (float)warn.rta/1000);
				else if (status==STATE_CRITICAL && host->rta_status==status)
					printf(" rta %0.3fms > %0.3fms", (float)host->rta / 1000, (float)crit.rta/1000);
			}
			/* pl text output */
			if (pl_mode) {
				if (status == STATE_OK)
					printf(" lost %u%%", host->pl);
				else if (status==STATE_WARNING && host->pl_status==status)
					printf(" lost %u%% > %u%%", host->pl, warn.pl);
				else if (status==STATE_CRITICAL && host->pl_status==status)
					printf(" lost %u%% > %u%%", host->pl, crit.pl);
			}
			/* jitter text output */
			if (jitter_mode) {
				if (status == STATE_OK)
					printf(" jitter %0.3fms", (float)host->jitter);
				else if (status==STATE_WARNING && host->jitter_status==status)
					printf(" jitter %0.3fms > %0.3fms", (float)host->jitter, warn.jitter);
				else if (status==STATE_CRITICAL && host->jitter_status==status)
					printf(" jitter %0.3fms > %0.3fms", (float)host->jitter, crit.jitter);
			}
			/* mos text output */
			if (mos_mode) {
				if (status == STATE_OK)
					printf(" MOS %0.1f", (float)host->mos);
				else if (status==STATE_WARNING && host->mos_status==status)
					printf(" MOS %0.1f < %0.1f", (float)host->mos, (float)warn.mos);
				else if (status==STATE_CRITICAL && host->mos_status==status)
					printf(" MOS %0.1f < %0.1f", (float)host->mos, (float)crit.mos);
			}
			/* score text output */
			if (score_mode) {
				if (status == STATE_OK)
					printf(" Score %u", (int)host->score);
				else if (status==STATE_WARNING && host->score_status==status )
					printf(" Score %u < %u", (int)host->score, (int)warn.score);
				else if (status==STATE_CRITICAL && host->score_status==status )
					printf(" Score %u < %u", (int)host->score, (int)crit.score);
			}
			/* order statis text output */
			if (order_mode) {
				if (status == STATE_OK)
					printf(" Packets in order");
				else if (status==STATE_CRITICAL && host->order_status==status)
					printf(" Packets out of order");
			}
		}
		host = host->next;
	}

	/* iterate once more for pretty perfparse output */
	if (!(!rta_mode && !pl_mode && !jitter_mode && !score_mode && !mos_mode && order_mode)) {
			printf("|");
	}
	i = 0;
	host = list;
	while(host) {
		if(debug) puts("");

		if (rta_mode) {
			if (host->pl<100) {
				printf("%srta=%0.3fms;%0.3f;%0.3f;0; %srtmax=%0.3fms;;;; %srtmin=%0.3fms;;;; ",
					(targets > 1) ? host->name : "",
					host->rta / 1000, (float)warn.rta / 1000, (float)crit.rta / 1000,
					(targets > 1) ? host->name : "", (float)host->rtmax / 1000,
					(targets > 1) ? host->name : "", (host->rtmin < INFINITY) ? (float)host->rtmin / 1000 : (float)0);
			} else {
				printf("%srta=U;;;; %srtmax=U;;;; %srtmin=U;;;; ",
					(targets > 1) ? host->name : "",
					(targets > 1) ? host->name : "",
					(targets > 1) ? host->name : "");
			}
		}

		if (pl_mode) {
			printf("%spl=%u%%;%u;%u;0;100 ", (targets > 1) ? host->name : "", host->pl, warn.pl, crit.pl);
		}

		if (jitter_mode) {
			if (host->pl<100) {
				printf("%sjitter_avg=%0.3fms;%0.3f;%0.3f;0; %sjitter_max=%0.3fms;;;; %sjitter_min=%0.3fms;;;; ",
					(targets > 1) ? host->name : "",
					(float)host->jitter,
					(float)warn.jitter,
					(float)crit.jitter,
					(targets > 1) ? host->name : "",
					(float)host->jitter_max / 1000, (targets > 1) ? host->name : "",
					(float)host->jitter_min / 1000
				);
			} else {
				printf("%sjitter_avg=U;;;; %sjitter_max=U;;;; %sjitter_min=U;;;; ",
					(targets > 1) ? host->name : "",
					(targets > 1) ? host->name : "",
					(targets > 1) ? host->name : "");
			}
		}

		if (mos_mode) {
			if (host->pl<100) {
				printf("%smos=%0.1f;%0.1f;%0.1f;0;5 ",
					(targets > 1) ? host->name : "",
					(float)host->mos,
					(float)warn.mos,
					(float)crit.mos);
			} else {
				printf("%smos=U;;;; ", (targets > 1) ? host->name : "");
			}
		}

		if (score_mode) {
			if (host->pl<100) {
				printf("%sscore=%u;%u;%u;0;100 ",
					(targets > 1) ? host->name : "",
					(int)host->score,
					(int)warn.score,
					(int)crit.score);
			} else {
				printf("%sscore=U;;;; ", (targets > 1) ? host->name : "");
			}
		}

		host = host->next;
	}

	if(min_hosts_alive > -1) {
		if(hosts_ok >= min_hosts_alive) status = STATE_OK;
		else if((hosts_ok + hosts_warn) >= min_hosts_alive) status = STATE_WARNING;
	}

	/* finish with an empty line */
	puts("");
	if(debug) printf("targets: %u, targets_alive: %u, hosts_ok: %u, hosts_warn: %u, min_hosts_alive: %i\n",
					 targets, targets_alive, hosts_ok, hosts_warn, min_hosts_alive);

	exit(status);
}

static u_int
get_timevaldiff(struct timeval *early, struct timeval *later)
{
	u_int ret;
	struct timeval now;

	if(!later) {
		gettimeofday(&now, &tz);
		later = &now;
	}
	if(!early) early = &prog_start;

	/* if early > later we return 0 so as to indicate a timeout */
	if(early->tv_sec > later->tv_sec ||
	   (early->tv_sec == later->tv_sec && early->tv_usec > later->tv_usec))
	{
		return 0;
	}
	ret = (later->tv_sec - early->tv_sec) * 1000000;
	ret += later->tv_usec - early->tv_usec;

	return ret;
}

static int
add_target_ip(char *arg, struct sockaddr_storage *in)
{
	struct rta_host *host;
	struct sockaddr_in *sin, *host_sin;
	struct sockaddr_in6 *sin6, *host_sin6;

	if (address_family == AF_INET)
		sin = (struct sockaddr_in *)in;
	else
		sin6 = (struct sockaddr_in6 *)in;



	/* disregard obviously stupid addresses
	 * (I didn't find an ipv6 equivalent to INADDR_NONE) */
	if (((address_family == AF_INET && (sin->sin_addr.s_addr == INADDR_NONE
					|| sin->sin_addr.s_addr == INADDR_ANY)))
		|| (address_family == AF_INET6 && (sin6->sin6_addr.s6_addr == in6addr_any.s6_addr))) {
		return -1;
	}

	/* no point in adding two identical IP's, so don't. ;) */
	host = list;
	while(host) {
		host_sin = (struct sockaddr_in *)&host->saddr_in;
		host_sin6 = (struct sockaddr_in6 *)&host->saddr_in;

		if(   (address_family == AF_INET && host_sin->sin_addr.s_addr == sin->sin_addr.s_addr)
		   || (address_family == AF_INET6 && host_sin6->sin6_addr.s6_addr == sin6->sin6_addr.s6_addr)) {
			if(debug) printf("Identical IP already exists. Not adding %s\n", arg);
			return -1;
		}
		host = host->next;
	}

	/* add the fresh ip */
	host = (struct rta_host*)malloc(sizeof(struct rta_host));
	if(!host) {
		char straddr[INET6_ADDRSTRLEN];
		parse_address((struct sockaddr_storage*)&in, straddr, sizeof(straddr));
		crash("add_target_ip(%s, %s): malloc(%lu) failed",
			arg, straddr, sizeof(struct rta_host));
	}
	memset(host, 0, sizeof(struct rta_host));

	/* set the values. use calling name for output */
	host->name = strdup(arg);


	/* fill out the sockaddr_storage struct */
	if(address_family == AF_INET) {
		host_sin = (struct sockaddr_in *)&host->saddr_in;
		host_sin->sin_family = AF_INET;
		host_sin->sin_addr.s_addr = sin->sin_addr.s_addr;
	}
	else {
		host_sin6 = (struct sockaddr_in6 *)&host->saddr_in;
		host_sin6->sin6_family = AF_INET6;
		memcpy(host_sin6->sin6_addr.s6_addr, sin6->sin6_addr.s6_addr, sizeof host_sin6->sin6_addr.s6_addr);
	}

	/* fill out the sockaddr_in struct */
	host->rtmin = INFINITY;
	host->rtmax = 0;
	host->jitter=0;
	host->jitter_max=0;
	host->jitter_min=INFINITY;
	host->last_tdiff=0;
	host->order_status=STATE_OK;
	host->last_icmp_seq=0;
	host->rta_status=0;
	host->pl_status=0;
	host->jitter_status=0;
	host->mos_status=0;
	host->score_status=0;
	host->pl_status=0;


	if(!list) list = cursor = host;
	else cursor->next = host;

	cursor = host;
	targets++;

	return 0;
}

/* wrapper for add_target_ip */
static int
add_target(char *arg)
{
	int error, result = -1;
	struct sockaddr_storage ip;
	struct addrinfo hints, *res, *p;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;

	switch (address_family) {
	case -1:
		/* -4 and -6 are not specified on cmdline */
		address_family = AF_INET;
		sin = (struct sockaddr_in *)&ip;
		result = inet_pton(address_family, arg, &sin->sin_addr);
#ifdef USE_IPV6
		if( result != 1 ){
			address_family = AF_INET6;
			sin6 = (struct sockaddr_in6 *)&ip;
			result = inet_pton(address_family, arg, &sin6->sin6_addr);
		}
#endif
		/* If we don't find any valid addresses, we still don't know the address_family */
		if ( result != 1) {
			address_family = -1;
		}
		break;
	case AF_INET:
		sin = (struct sockaddr_in *)&ip;
		result = inet_pton(address_family, arg, &sin->sin_addr);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&ip;
		result = inet_pton(address_family, arg, &sin6->sin6_addr);
		break;
	default: crash("Address family not supported");
	}

	/* don't resolve if we don't have to */
	if(result == 1) {
		/* don't add all ip's if we were given a specific one */
		return add_target_ip(arg, &ip);
	}
	else {
		errno = 0;
		memset(&hints, 0, sizeof(hints));
		if (address_family == -1) {
			hints.ai_family = AF_UNSPEC;
		} else {
			hints.ai_family = address_family == AF_INET ? PF_INET : PF_INET6;
		}
		hints.ai_socktype = SOCK_RAW;
		if((error = getaddrinfo(arg, NULL, &hints, &res)) != 0) {
			errno = 0;
			crash("Failed to resolve %s: %s", arg, gai_strerror(error));
			return -1;
		}
		address_family = res->ai_family;
	}

	/* possibly add all the IP's as targets */
	for(p = res; p != NULL; p = p->ai_next) {
		memcpy(&ip, p->ai_addr, p->ai_addrlen);
		add_target_ip(arg, &ip);

		/* this is silly, but it works */
		if(mode == MODE_HOSTCHECK || mode == MODE_ALL) {
			if(debug > 2) printf("mode: %d\n", mode);
			continue;
		}
		break;
	}
	freeaddrinfo(res);

	return 0;
}

static void
set_source_ip(char *arg)
{
	struct sockaddr_in src;

	memset(&src, 0, sizeof(src));
	src.sin_family = address_family;
	if((src.sin_addr.s_addr = inet_addr(arg)) == INADDR_NONE)
		src.sin_addr.s_addr = get_ip_address(arg);
	if(bind(icmp_sock, (struct sockaddr *)&src, sizeof(src)) == -1)
		crash("Cannot bind to IP address %s", arg);
}

/* TODO: Move this to netutils.c and also change check_dhcp to use that. */
static in_addr_t
get_ip_address(const char *ifname)
{
  // TODO: Rewrite this so the function return an error and we exit somewhere else
	struct sockaddr_in ip;
	ip.sin_addr.s_addr = 0; // Fake initialization to make compiler happy
#if defined(SIOCGIFADDR)
	struct ifreq ifr;

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

	if(ioctl(icmp_sock, SIOCGIFADDR, &ifr) == -1)
		crash("Cannot determine IP address of interface %s", ifname);

	memcpy(&ip, &ifr.ifr_addr, sizeof(ip));
#else
	(void) ifname;
	errno = 0;
	crash("Cannot get interface IP address on this platform.");
#endif
	return ip.sin_addr.s_addr;
}

/*
 * u = micro
 * m = milli
 * s = seconds
 * return value is in microseconds
 */
static u_int
get_timevar(const char *str)
{
	char p, u, *ptr;
	size_t len;
	u_int i, d;	            /* integer and decimal, respectively */
	u_int factor = 1000;    /* default to milliseconds */

	if(!str) return 0;
	len = strlen(str);
	if(!len) return 0;

	/* unit might be given as ms|m (millisec),
	 * us|u (microsec) or just plain s, for seconds */
	p = '\0';
	u = str[len - 1];
	if(len >= 2 && !isdigit((int)str[len - 2])) p = str[len - 2];
	if(p && u == 's') u = p;
	else if(!p) p = u;
	if(debug > 2) printf("evaluating %s, u: %c, p: %c\n", str, u, p);

	if(u == 'u') factor = 1;            /* microseconds */
	else if(u == 'm') factor = 1000;	/* milliseconds */
	else if(u == 's') factor = 1000000;	/* seconds */
	if(debug > 2) printf("factor is %u\n", factor);

	i = strtoul(str, &ptr, 0);
	if(!ptr || *ptr != '.' || strlen(ptr) < 2 || factor == 1)
		return i * factor;

	/* time specified in usecs can't have decimal points, so ignore them */
	if(factor == 1) return i;

	d = strtoul(ptr + 1, NULL, 0);

	/* d is decimal, so get rid of excess digits */
	while(d >= factor) d /= 10;

	/* the last parenthesis avoids floating point exceptions. */
	return ((i * factor) + (d * (factor / 10)));
}

/* not too good at checking errors, but it'll do (main() should barfe on -1) */
static int
get_threshold(char *str, threshold *th)
{
	char *p = NULL, i = 0;

	if(!str || !strlen(str) || !th) return -1;

	/* pointer magic slims code by 10 lines. i is bof-stop on stupid libc's */
	p = &str[strlen(str) - 1];
	while(p != &str[1]) {
		if(*p == '%') *p = '\0';
		else if(*p == ',' && i) {
			*p = '\0';	/* reset it so get_timevar(str) works nicely later */
			th->pl = (unsigned char)strtoul(p+1, NULL, 0);
			break;
		}
		i = 1;
		p--;
	}
	th->rta = get_timevar(str);

	if(!th->rta) return -1;

	if(th->rta > MAXTTL * 1000000) th->rta = MAXTTL * 1000000;
	if(th->pl > 100) th->pl = 100;

	return 0;
}

/*
 * This functions receives a pointer to a string which should contain a threshold for the
 * rta, packet_loss, jitter, mos or score mode in the form number,number[m|%]* assigns the
 * parsed number to the corresponding threshold variable.
 * @param[in,out] str String containing the given threshold values
 * @param[in] length strlen(str)
 * @param[out] warn Pointer to the warn threshold struct to which the values should be assigned
 * @param[out] crit Pointer to the crit threshold struct to which the values should be assigned
 * @param[in] mode Determines whether this a threshold for rta, packet_loss, jitter, mos or score (exclusively)
 */
static bool get_threshold2(char *str, size_t length, threshold *warn, threshold *crit, threshold_mode mode) {
	if (!str || !length || !warn || !crit) return false;


	// p points to the last char in str
	char *p = &str[length - 1];

	// first_iteration is bof-stop on stupid libc's
	bool first_iteration = true;

	while(p != &str[0]) {
		if( (*p == 'm') || (*p == '%') ) {
			*p = '\0';
		} else if(*p == ',' && !first_iteration) {
			*p = '\0';	/* reset it so get_timevar(str) works nicely later */

			char *start_of_value = p + 1;

			if (!parse_threshold2_helper(start_of_value, strlen(start_of_value), crit, mode)){
				return false;
			}

		}
		first_iteration = false;
		p--;
	}

	return parse_threshold2_helper(p, strlen(p), warn, mode);
}

static bool parse_threshold2_helper(char *s, size_t length, threshold *thr, threshold_mode mode) {
	char *resultChecker = {0};

	switch (mode) {
		case const_rta_mode:
			thr->rta = strtod(s, &resultChecker) * 1000;
			break;
		case const_packet_loss_mode:
			thr->pl = (unsigned char)strtoul(s, &resultChecker, 0);
			break;
		case const_jitter_mode:
			thr->jitter = strtod(s, &resultChecker);

			break;
		case const_mos_mode:
			thr->mos = strtod(s, &resultChecker);
			break;
		case const_score_mode:
			thr->score = strtod(s, &resultChecker);
			break;
	}

	if (resultChecker == s) {
		// Failed to parse
		return false;
	}

	if (resultChecker != (s + length)) {
		// Trailing symbols
		return false;
	}

	return true;
}

unsigned short
icmp_checksum(uint16_t *p, size_t n)
{
	unsigned short cksum;
	long sum = 0;

	/* sizeof(uint16_t) == 2 */
	while(n >= 2) {
		sum += *(p++);
		n -= 2;
	}

	/* mop up the occasional odd byte */
	if(n == 1) sum += *((uint8_t *)p -1);

	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	cksum = ~sum;				/* ones-complement, trunc to 16 bits */

	return cksum;
}

void
print_help(void)
{
	/*print_revision (progname);*/ /* FIXME: Why? */
	printf ("Copyright (c) 2005 Andreas Ericsson <ae@op5.se>\n");

	printf (COPYRIGHT, copyright, email);

	printf ("\n\n");

	print_usage ();

	printf (UT_HELP_VRSN);
	printf (UT_EXTRA_OPTS);

	printf (" %s\n", "-H");
	printf ("    %s\n", _("specify a target"));
	printf (" %s\n", "[-4|-6]");
	printf ("    %s\n", _("Use IPv4 (default) or IPv6 to communicate with the targets"));
	printf (" %s\n", "-w");
	printf ("    %s", _("warning threshold (currently "));
	printf ("%0.3fms,%u%%)\n", (float)warn.rta / 1000, warn.pl);
	printf (" %s\n", "-c");
	printf ("    %s", _("critical threshold (currently "));
	printf ("%0.3fms,%u%%)\n", (float)crit.rta / 1000, crit.pl);

	printf (" %s\n", "-R");
	printf ("    %s\n", _("RTA, round trip average,  mode  warning,critical, ex. 100ms,200ms unit in ms"));
	printf (" %s\n", "-P");
	printf ("    %s\n", _("packet loss mode, ex. 40%,50% , unit in %"));
	printf (" %s\n", "-J");
	printf ("    %s\n", _("jitter mode  warning,critical, ex. 40.000ms,50.000ms , unit in ms "));
	printf (" %s\n", "-M");
	printf ("    %s\n", _("MOS mode, between 0 and 4.4  warning,critical, ex. 3.5,3.0"));
	printf (" %s\n", "-S");
	printf ("    %s\n", _("score  mode, max value 100  warning,critical, ex. 80,70 "));
	printf (" %s\n", "-O");
	printf ("    %s\n", _("detect out of order ICMP packts "));
	printf (" %s\n", "-H");
	printf ("    %s\n", _("specify a target"));
	printf (" %s\n", "-s");
	printf ("    %s\n", _("specify a source IP address or device name"));
	printf (" %s\n", "-n");
	printf ("    %s", _("number of packets to send (currently "));
	printf ("%u)\n",packets);
	printf (" %s\n", "-p");
	printf ("    %s", _("number of packets to send (currently "));
	printf ("%u)\n",packets);
	printf (" %s\n", "-i");
	printf ("    %s", _("max packet interval (currently "));
	printf ("%0.3fms)\n",(float)pkt_interval / 1000);
	printf (" %s\n", "-I");
	printf ("    %s", _("max target interval (currently "));
	printf ("%0.3fms)\n", (float)target_interval / 1000);
	printf (" %s\n", "-m");
	printf ("    %s",_("number of alive hosts required for success"));
	printf ("\n");
	printf (" %s\n", "-l");
	printf ("    %s", _("TTL on outgoing packets (currently "));
	printf ("%u)\n", ttl);
	printf (" %s\n", "-t");
	printf ("    %s",_("timeout value (seconds, currently  "));
	printf ("%u)\n", timeout);
	printf (" %s\n", "-b");
	printf ("    %s\n", _("Number of icmp data bytes to send"));
	printf ("    %s %u + %d)\n", _("Packet size will be data bytes + icmp header (currently"),icmp_data_size, ICMP_MINLEN);
	printf (" %s\n", "-v");
	printf ("    %s\n", _("verbose"));
	printf ("\n");
	printf ("%s\n", _("Notes:"));
	printf (" %s\n", _("If none of R,P,J,M,S or O is specified, default behavior is -R -P"));
	printf (" %s\n", _("The -H switch is optional. Naming a host (or several) to check is not."));
	printf ("\n");
	printf (" %s\n", _("Threshold format for -w and -c is 200.25,60% for 200.25 msec RTA and 60%"));
	printf (" %s\n", _("packet loss.  The default values should work well for most users."));
	printf (" %s\n", _("You can specify different RTA factors using the standardized abbreviations"));
	printf (" %s\n", _("us (microseconds), ms (milliseconds, default) or just plain s for seconds."));
	/* -d not yet implemented */
	/*  printf ("%s\n", _("Threshold format for -d is warn,crit.  12,14 means WARNING if >= 12 hops"));
			printf ("%s\n", _("are spent and CRITICAL if >= 14 hops are spent."));
			printf ("%s\n\n", _("NOTE: Some systems decrease TTL when forming ICMP_ECHOREPLY, others do not."));*/
	printf ("\n");
	printf (" %s\n", _("The -v switch can be specified several times for increased verbosity."));
	/*  printf ("%s\n", _("Long options are currently unsupported."));
			printf ("%s\n", _("Options marked with * require an argument"));
			*/

	printf (UT_SUPPORT);
}



void
print_usage (void)
{
	printf ("%s\n", _("Usage:"));
	printf(" %s [options] [-H] host1 host2 hostN\n", progname);
}
