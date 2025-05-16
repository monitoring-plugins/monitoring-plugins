/*****************************************************************************
 *
 * Monitoring check_icmp plugin
 *
 * License: GPL
 * Copyright (c) 2005-2024 Monitoring Plugins Development Team
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
const char *copyright = "2005-2024";
const char *email = "devel@monitoring-plugins.org";

/** Monitoring Plugins basic includes */
#include "../plugins/common.h"
#include "netutils.h"
#include "utils.h"

#if HAVE_SYS_SOCKIO_H
#	include <sys/sockio.h>
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
#include <math.h>
#include <netdb.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>

#include "../lib/states.h"
#include "./check_icmp.d/config.h"
#include "./check_icmp.d/check_icmp_helpers.h"

/** sometimes undefined system macros (quite a few, actually) **/
#ifndef MAXTTL
#	define MAXTTL 255
#endif
#ifndef INADDR_NONE
#	define INADDR_NONE (in_addr_t)(-1)
#endif

#ifndef SOL_IP
#	define SOL_IP 0
#endif

/* we bundle these in one #ifndef, since they're all from BSD
 * Put individual #ifndef's around those that bother you */
#ifndef ICMP_UNREACH_NET_UNKNOWN
#	define ICMP_UNREACH_NET_UNKNOWN  6
#	define ICMP_UNREACH_HOST_UNKNOWN 7
#	define ICMP_UNREACH_ISOLATED     8
#	define ICMP_UNREACH_NET_PROHIB   9
#	define ICMP_UNREACH_HOST_PROHIB  10
#	define ICMP_UNREACH_TOSNET       11
#	define ICMP_UNREACH_TOSHOST      12
#endif
/* tru64 has the ones above, but not these */
#ifndef ICMP_UNREACH_FILTER_PROHIB
#	define ICMP_UNREACH_FILTER_PROHIB     13
#	define ICMP_UNREACH_HOST_PRECEDENCE   14
#	define ICMP_UNREACH_PRECEDENCE_CUTOFF 15
#endif

#define FLAG_LOST_CAUSE 0x01 /* decidedly dead target. */

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

enum enum_threshold_mode {
	const_rta_mode,
	const_packet_loss_mode,
	const_jitter_mode,
	const_mos_mode,
	const_score_mode
};

typedef enum enum_threshold_mode threshold_mode;

/** prototypes **/
void print_help();
void print_usage(void);

/* Time related */
static unsigned int get_timevar(const char *str);
static time_t get_timevaldiff(struct timeval earlier, struct timeval later);
static time_t get_timevaldiff_to_now(struct timeval earlier);

static in_addr_t get_ip_address(const char *ifname);
static void set_source_ip(char *arg, int icmp_sock);

/* Receiving data */
static int wait_for_reply(int socket, time_t time_interval, unsigned short icmp_pkt_size,
						  unsigned int *pkt_interval, unsigned int *target_interval,
						  uint16_t sender_id, ping_target **table, unsigned short packets,
						  unsigned short number_of_targets, check_icmp_state *program_state);

static ssize_t recvfrom_wto(int sock, void *buf, unsigned int len, struct sockaddr *saddr,
							time_t *timeout, struct timeval *received_timestamp);
static int handle_random_icmp(unsigned char *packet, struct sockaddr_storage *addr,
							  unsigned int *pkt_interval, unsigned int *target_interval,
							  uint16_t sender_id, ping_target **table, unsigned short packets,
							  unsigned short number_of_targets, check_icmp_state *program_state);

/* Sending data */
static int send_icmp_ping(int socket, ping_target *host, unsigned short icmp_pkt_size,
						  uint16_t sender_id, check_icmp_state *program_state);

/* Threshold related */
typedef struct {
	int errorcode;
	check_icmp_threshold threshold;
} get_threshold_wrapper;
static get_threshold_wrapper get_threshold(char *str, check_icmp_threshold threshold);

typedef struct {
	int errorcode;
	check_icmp_threshold warn;
	check_icmp_threshold crit;
} get_threshold2_wrapper;
static get_threshold2_wrapper get_threshold2(char *str, size_t length, check_icmp_threshold warn,
											 check_icmp_threshold crit, threshold_mode mode);

typedef struct {
	int errorcode;
	check_icmp_threshold result;
} parse_threshold2_helper_wrapper;
static parse_threshold2_helper_wrapper parse_threshold2_helper(char *threshold_string,
															   size_t length,
															   check_icmp_threshold thr,
															   threshold_mode mode);

/* main test function */
static void run_checks(bool order_mode, bool mos_mode, bool rta_mode, bool pl_mode,
					   bool jitter_mode, bool score_mode, int min_hosts_alive,
					   unsigned short icmp_pkt_size, unsigned int *pkt_interval,
					   unsigned int *target_interval, check_icmp_threshold warn,
					   check_icmp_threshold crit, uint16_t sender_id,
					   check_icmp_execution_mode mode, unsigned int max_completion_time,
					   struct timeval prog_start, ping_target **table, unsigned short packets,
					   int icmp_sock, unsigned short number_of_targets,
					   check_icmp_state *program_state, ping_target *target_list);

/* Target aquisition */
typedef struct {
	int error_code;
	check_icmp_target_container host;
} add_host_wrapper;
static add_host_wrapper add_host(char *arg, check_icmp_execution_mode mode);
typedef struct {
	int error_code;
	ping_target *targets;
	unsigned int number_of_targets;
} add_target_wrapper;
static add_target_wrapper add_target(char *arg, check_icmp_execution_mode mode);

typedef struct {
	int error_code;
	ping_target *target;
} add_target_ip_wrapper;
static add_target_ip_wrapper add_target_ip(char *arg, struct sockaddr_storage *address);

static void parse_address(struct sockaddr_storage *addr, char *address, socklen_t size);

static unsigned short icmp_checksum(uint16_t *packet, size_t packet_size);

/* End of run function */
static void finish(int /*sig*/, bool order_mode, bool mos_mode, bool rta_mode, bool pl_mode,
				   bool jitter_mode, bool score_mode, int min_hosts_alive,
				   check_icmp_threshold warn, check_icmp_threshold crit, int icmp_sock,
				   unsigned short number_of_targets, check_icmp_state *program_state,
				   ping_target *target_list);

/* Error exit */
static void crash(const char *fmt, ...);

/** global variables **/
static int debug = 0;

extern unsigned int timeout;

/** the working code **/
static inline unsigned short targets_alive(unsigned short targets, unsigned short targets_down) {
	return targets - targets_down;
}
static inline unsigned int icmp_pkts_en_route(unsigned int icmp_sent, unsigned int icmp_recv,
											  unsigned int icmp_lost) {
	return icmp_sent - (icmp_recv + icmp_lost);
}

// Create configuration from cli parameters
typedef struct {
	int errorcode;
	check_icmp_config config;
} check_icmp_config_wrapper;
check_icmp_config_wrapper process_arguments(int argc, char **argv);

check_icmp_config_wrapper process_arguments(int argc, char **argv) {
	/* get calling name the old-fashioned way for portability instead
	 * of relying on the glibc-ism __progname */
	char *ptr = strrchr(argv[0], '/');
	if (ptr) {
		progname = &ptr[1];
	} else {
		progname = argv[0];
	}

	check_icmp_config_wrapper result = {
		.errorcode = OK,
		.config = check_icmp_config_init(),
	};

	/* use the pid to mark packets as ours */
	/* Some systems have 32-bit pid_t so mask off only 16 bits */
	result.config.sender_id = getpid() & 0xffff;

	if (!strcmp(progname, "check_icmp") || !strcmp(progname, "check_ping")) {
		result.config.mode = MODE_ICMP;
	} else if (!strcmp(progname, "check_host")) {
		result.config.mode = MODE_HOSTCHECK;
		result.config.pkt_interval = 1000000;
		result.config.number_of_packets = 5;
		result.config.crit.rta = result.config.warn.rta = 1000000;
		result.config.crit.pl = result.config.warn.pl = 100;
	} else if (!strcmp(progname, "check_rta_multi")) {
		result.config.mode = MODE_ALL;
		result.config.target_interval = 0;
		result.config.pkt_interval = 50000;
		result.config.number_of_packets = 5;
	}
	/* support "--help" and "--version" */
	if (argc == 2) {
		if (!strcmp(argv[1], "--help")) {
			strcpy(argv[1], "-h");
		}
		if (!strcmp(argv[1], "--version")) {
			strcpy(argv[1], "-V");
		}
	}

	// Parse protocol arguments first
	// and count hosts here
	char *opts_str = "vhVw:c:n:p:t:H:s:i:b:I:l:m:P:R:J:S:M:O64";
	for (int i = 1; i < argc; i++) {
		long int arg;
		while ((arg = getopt(argc, argv, opts_str)) != EOF) {
			switch (arg) {
			case '4':
				if (address_family != -1) {
					crash("Multiple protocol versions not supported");
				}
				address_family = AF_INET;
				break;
			case '6':
				if (address_family != -1) {
					crash("Multiple protocol versions not supported");
				}
				address_family = AF_INET6;
				break;
			case 'H': {
				result.config.number_of_hosts++;
			}
			}
		}
	}

	char **tmp = &argv[optind];
	while (*tmp) {
		result.config.number_of_hosts++;
		tmp++;
	}

	// Sanity check: if hostmode is selected,only a single host is allowed
	if (result.config.mode == MODE_HOSTCHECK && result.config.number_of_hosts > 1) {
		usage("check_host only allows a single host");
	}

	// Allocate hosts
	result.config.hosts =
		calloc(result.config.number_of_hosts, sizeof(check_icmp_target_container));
	if (result.config.hosts == NULL) {
		crash("failed to allocate memory");
	}

	/* Reset argument scanning */
	optind = 1;

	/* parse the arguments */
	for (int i = 1; i < argc; i++) {
		long int arg;
		while ((arg = getopt(argc, argv, opts_str)) != EOF) {
			switch (arg) {
			case 'v':
				debug++;
				break;
			case 'b': {
				long size = strtol(optarg, NULL, 0);
				if ((unsigned long)size >= (sizeof(struct icmp) + sizeof(struct icmp_ping_data)) &&
					size < MAX_PING_DATA) {
					result.config.icmp_data_size = (unsigned short)size;
					result.config.icmp_pkt_size = (unsigned short)(size + ICMP_MINLEN);
				} else {
					usage_va("ICMP data length must be between: %lu and %lu",
							 sizeof(struct icmp) + sizeof(struct icmp_ping_data),
							 MAX_PING_DATA - 1);
				}
			} break;
			case 'i':
				result.config.pkt_interval = get_timevar(optarg);
				break;
			case 'I':
				result.config.target_interval = get_timevar(optarg);
				break;
			case 'w': {
				get_threshold_wrapper warn = get_threshold(optarg, result.config.warn);
				if (warn.errorcode == OK) {
					result.config.warn = warn.threshold;
				} else {
					crash("failed to parse warning threshold");
				}
			} break;
			case 'c': {
				get_threshold_wrapper crit = get_threshold(optarg, result.config.crit);
				if (crit.errorcode == OK) {
					result.config.crit = crit.threshold;
				} else {
					crash("failed to parse critical threshold");
				}
			} break;
			case 'n':
			case 'p':
				result.config.number_of_packets = (unsigned short)strtoul(optarg, NULL, 0);
				if (result.config.number_of_packets > 20) {
					errno = 0;
					crash("packets is > 20 (%d)", result.config.number_of_packets);
				}
				break;
			case 't':
				timeout = (unsigned int)strtoul(optarg, NULL, 0);
				// TODO die here and complain about wrong input
				break;
			case 'H': {
				add_target_wrapper add_result = add_target(optarg, result.config.mode);
				if (add_result.error_code == OK) {
					if (result.config.targets != NULL) {
						result.config.number_of_targets +=
							ping_target_list_append(result.config.targets, add_result.targets);
					} else {
						result.config.targets = add_result.targets;
						result.config.number_of_targets += add_result.number_of_targets;
					}
				}
			} break;
			case 'l':
				result.config.ttl = strtoul(optarg, NULL, 0);
				break;
			case 'm':
				result.config.min_hosts_alive = (int)strtoul(optarg, NULL, 0);
				break;
			case 's': /* specify source IP address */
				result.config.source_ip = optarg;
				break;
			case 'V': /* version */
				print_revision(progname, NP_VERSION);
				exit(STATE_UNKNOWN);
			case 'h': /* help */
				print_help();
				exit(STATE_UNKNOWN);
				break;
			case 'R': /* RTA mode */ {
				get_threshold2_wrapper rta_th = get_threshold2(
					optarg, strlen(optarg), result.config.warn, result.config.crit, const_rta_mode);

				if (rta_th.errorcode != OK) {
					crash("Failed to parse RTA threshold");
				}

				result.config.warn = rta_th.warn;
				result.config.crit = rta_th.crit;
				result.config.rta_mode = true;
			} break;
			case 'P': /* packet loss mode */ {
				get_threshold2_wrapper pl_th =
					get_threshold2(optarg, strlen(optarg), result.config.warn, result.config.crit,
								   const_packet_loss_mode);
				if (pl_th.errorcode != OK) {
					crash("Failed to parse packet loss threshold");
				}

				result.config.warn = pl_th.warn;
				result.config.crit = pl_th.crit;
				result.config.pl_mode = true;
			} break;
			case 'J': /* jitter mode */ {
				get_threshold2_wrapper jitter_th =
					get_threshold2(optarg, strlen(optarg), result.config.warn, result.config.crit,
								   const_jitter_mode);
				if (jitter_th.errorcode != OK) {
					crash("Failed to parse jitter threshold");
				}

				result.config.warn = jitter_th.warn;
				result.config.crit = jitter_th.crit;
				result.config.jitter_mode = true;
			} break;
			case 'M': /* MOS mode */ {
				get_threshold2_wrapper mos_th = get_threshold2(
					optarg, strlen(optarg), result.config.warn, result.config.crit, const_mos_mode);
				if (mos_th.errorcode != OK) {
					crash("Failed to parse MOS threshold");
				}

				result.config.warn = mos_th.warn;
				result.config.crit = mos_th.crit;
				result.config.mos_mode = true;
			} break;
			case 'S': /* score mode */ {
				get_threshold2_wrapper score_th =
					get_threshold2(optarg, strlen(optarg), result.config.warn, result.config.crit,
								   const_score_mode);
				if (score_th.errorcode != OK) {
					crash("Failed to parse score threshold");
				}

				result.config.warn = score_th.warn;
				result.config.crit = score_th.crit;
				result.config.score_mode = true;
			} break;
			case 'O': /* out of order mode */
				result.config.order_mode = true;
				break;
			}
		}
	}

	argv = &argv[optind];
	while (*argv) {
		add_target(*argv, result.config.mode);
		argv++;
	}

	if (!result.config.number_of_targets) {
		errno = 0;
		crash("No hosts to check");
	}

	/* stupid users should be able to give whatever thresholds they want
	 * (nothing will break if they do), but some anal plugin maintainer
	 * will probably add some printf() thing here later, so it might be
	 * best to at least show them where to do it. ;) */
	if (result.config.warn.pl > result.config.crit.pl) {
		result.config.warn.pl = result.config.crit.pl;
	}
	if (result.config.warn.rta > result.config.crit.rta) {
		result.config.warn.rta = result.config.crit.rta;
	}
	if (result.config.warn.jitter > result.config.crit.jitter) {
		result.config.crit.jitter = result.config.warn.jitter;
	}
	if (result.config.warn.mos < result.config.crit.mos) {
		result.config.warn.mos = result.config.crit.mos;
	}
	if (result.config.warn.score < result.config.crit.score) {
		result.config.warn.score = result.config.crit.score;
	}

	return result;
}

/** code start **/
static void crash(const char *fmt, ...) {

	printf("%s: ", progname);

	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	if (errno) {
		printf(": %s", strerror(errno));
	}
	puts("");

	exit(3);
}

static const char *get_icmp_error_msg(unsigned char icmp_type, unsigned char icmp_code) {
	const char *msg = "unreachable";

	if (debug > 1) {
		printf("get_icmp_error_msg(%u, %u)\n", icmp_type, icmp_code);
	}
	switch (icmp_type) {
	case ICMP_UNREACH:
		switch (icmp_code) {
		case ICMP_UNREACH_NET:
			msg = "Net unreachable";
			break;
		case ICMP_UNREACH_HOST:
			msg = "Host unreachable";
			break;
		case ICMP_UNREACH_PROTOCOL:
			msg = "Protocol unreachable (firewall?)";
			break;
		case ICMP_UNREACH_PORT:
			msg = "Port unreachable (firewall?)";
			break;
		case ICMP_UNREACH_NEEDFRAG:
			msg = "Fragmentation needed";
			break;
		case ICMP_UNREACH_SRCFAIL:
			msg = "Source route failed";
			break;
		case ICMP_UNREACH_ISOLATED:
			msg = "Source host isolated";
			break;
		case ICMP_UNREACH_NET_UNKNOWN:
			msg = "Unknown network";
			break;
		case ICMP_UNREACH_HOST_UNKNOWN:
			msg = "Unknown host";
			break;
		case ICMP_UNREACH_NET_PROHIB:
			msg = "Network denied (firewall?)";
			break;
		case ICMP_UNREACH_HOST_PROHIB:
			msg = "Host denied (firewall?)";
			break;
		case ICMP_UNREACH_TOSNET:
			msg = "Bad TOS for network (firewall?)";
			break;
		case ICMP_UNREACH_TOSHOST:
			msg = "Bad TOS for host (firewall?)";
			break;
		case ICMP_UNREACH_FILTER_PROHIB:
			msg = "Prohibited by filter (firewall)";
			break;
		case ICMP_UNREACH_HOST_PRECEDENCE:
			msg = "Host precedence violation";
			break;
		case ICMP_UNREACH_PRECEDENCE_CUTOFF:
			msg = "Precedence cutoff";
			break;
		default:
			msg = "Invalid code";
			break;
		}
		break;

	case ICMP_TIMXCEED:
		/* really 'out of reach', or non-existent host behind a router serving
		 * two different subnets */
		switch (icmp_code) {
		case ICMP_TIMXCEED_INTRANS:
			msg = "Time to live exceeded in transit";
			break;
		case ICMP_TIMXCEED_REASS:
			msg = "Fragment reassembly time exceeded";
			break;
		default:
			msg = "Invalid code";
			break;
		}
		break;

	case ICMP_SOURCEQUENCH:
		msg = "Transmitting too fast";
		break;
	case ICMP_REDIRECT:
		msg = "Redirect (change route)";
		break;
	case ICMP_PARAMPROB:
		msg = "Bad IP header (required option absent)";
		break;

		/* the following aren't error messages, so ignore */
	case ICMP_TSTAMP:
	case ICMP_TSTAMPREPLY:
	case ICMP_IREQ:
	case ICMP_IREQREPLY:
	case ICMP_MASKREQ:
	case ICMP_MASKREPLY:
	default:
		msg = "";
		break;
	}

	return msg;
}

static int handle_random_icmp(unsigned char *packet, struct sockaddr_storage *addr,
							  unsigned int *pkt_interval, unsigned int *target_interval,
							  const uint16_t sender_id, ping_target **table, unsigned short packets,
							  const unsigned short number_of_targets,
							  check_icmp_state *program_state) {
	struct icmp icmp_packet;
	memcpy(&icmp_packet, packet, sizeof(icmp_packet));
	if (icmp_packet.icmp_type == ICMP_ECHO && ntohs(icmp_packet.icmp_id) == sender_id) {
		/* echo request from us to us (pinging localhost) */
		return 0;
	}

	if (debug) {
		printf("handle_random_icmp(%p, %p)\n", (void *)&icmp_packet, (void *)addr);
	}

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
	if (icmp_packet.icmp_type != ICMP_UNREACH && icmp_packet.icmp_type != ICMP_TIMXCEED &&
		icmp_packet.icmp_type != ICMP_SOURCEQUENCH && icmp_packet.icmp_type != ICMP_PARAMPROB) {
		return 0;
	}

	/* might be for us. At least it holds the original package (according
	 * to RFC 792). If it isn't, just ignore it */
	struct icmp sent_icmp;
	memcpy(&sent_icmp, packet + 28, sizeof(sent_icmp));
	if (sent_icmp.icmp_type != ICMP_ECHO || ntohs(sent_icmp.icmp_id) != sender_id ||
		ntohs(sent_icmp.icmp_seq) >= number_of_targets * packets) {
		if (debug) {
			printf("Packet is no response to a packet we sent\n");
		}
		return 0;
	}

	/* it is indeed a response for us */
	ping_target *host = table[ntohs(sent_icmp.icmp_seq) / packets];
	if (debug) {
		char address[INET6_ADDRSTRLEN];
		parse_address(addr, address, sizeof(address));
		printf("Received \"%s\" from %s for ICMP ECHO sent to %s.\n",
			   get_icmp_error_msg(icmp_packet.icmp_type, icmp_packet.icmp_code), address,
			   host->name);
	}

	program_state->icmp_lost++;
	host->icmp_lost++;
	/* don't spend time on lost hosts any more */
	if (host->flags & FLAG_LOST_CAUSE) {
		return 0;
	}

	/* source quench means we're sending too fast, so increase the
	 * interval and mark this packet lost */
	if (icmp_packet.icmp_type == ICMP_SOURCEQUENCH) {
		*pkt_interval = (unsigned int)(*pkt_interval * PACKET_BACKOFF_FACTOR);
		*target_interval = (unsigned int)(*target_interval * TARGET_BACKOFF_FACTOR);
	} else {
		program_state->targets_down++;
		host->flags |= FLAG_LOST_CAUSE;
	}
	host->icmp_type = icmp_packet.icmp_type;
	host->icmp_code = icmp_packet.icmp_code;
	host->error_addr = *addr;

	return 0;
}

void parse_address(struct sockaddr_storage *addr, char *address, socklen_t size) {
	switch (address_family) {
	case AF_INET:
		inet_ntop(address_family, &((struct sockaddr_in *)addr)->sin_addr, address, size);
		break;
	case AF_INET6:
		inet_ntop(address_family, &((struct sockaddr_in6 *)addr)->sin6_addr, address, size);
		break;
	}
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	address_family = -1;
	int icmp_proto = IPPROTO_ICMP;

	/* POSIXLY_CORRECT might break things, so unset it (the portable way) */
	environ = NULL;

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_icmp_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode != OK) {
		crash("failed to parse config");
	}

	const check_icmp_config config = tmp_config.config;

	// add_target might change address_family
	switch (address_family) {
	case AF_INET:
		icmp_proto = IPPROTO_ICMP;
		break;
	case AF_INET6:
		icmp_proto = IPPROTO_ICMPV6;
		break;
	default:
		crash("Address family not supported");
	}

	int icmp_sock = socket(address_family, SOCK_RAW, icmp_proto);
	if (icmp_sock == -1) {
		crash("Failed to obtain ICMP socket");
	}

	if (config.source_ip) {
		set_source_ip(config.source_ip, icmp_sock);
	}

#ifdef SO_TIMESTAMP
	int on = 1;
	if (setsockopt(icmp_sock, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on))) {
		if (debug) {
			printf("Warning: no SO_TIMESTAMP support\n");
		}
	}
#endif // SO_TIMESTAMP

	/* now drop privileges (no effect if not setsuid or geteuid() == 0) */
	if (setuid(getuid()) == -1) {
		printf("ERROR: Failed to drop privileges\n");
		return 1;
	}

	if (icmp_sock) {
		int result = setsockopt(icmp_sock, SOL_IP, IP_TTL, &config.ttl, sizeof(config.ttl));
		if (debug) {
			if (result == -1) {
				printf("setsockopt failed\n");
			} else {
				printf("ttl set to %lu\n", config.ttl);
			}
		}
	}

	struct sigaction sig_action;
	sig_action.sa_handler = NULL;
	sig_action.sa_sigaction = check_icmp_timeout_handler;
	sigfillset(&sig_action.sa_mask);
	sig_action.sa_flags = SA_NODEFER | SA_RESTART | SA_SIGINFO;

	sigaction(SIGINT, &sig_action, NULL);
	sigaction(SIGHUP, &sig_action, NULL);
	sigaction(SIGTERM, &sig_action, NULL);
	sigaction(SIGALRM, &sig_action, NULL);
	if (debug) {
		printf("Setting alarm timeout to %u seconds\n", timeout);
	}
	alarm(timeout);

	/* make sure we don't wait any longer than necessary */
	struct timeval prog_start;
	gettimeofday(&prog_start, NULL);

	unsigned int max_completion_time =
		((config.number_of_targets * config.number_of_packets * config.pkt_interval) +
		 (config.number_of_targets * config.target_interval)) +
		(config.number_of_targets * config.number_of_packets * config.crit.rta) + config.crit.rta;

	if (debug) {
		printf("packets: %u, targets: %u\n"
			   "target_interval: %0.3f, pkt_interval %0.3f\n"
			   "crit.rta: %0.3f\n"
			   "max_completion_time: %0.3f\n",
			   config.number_of_packets, config.number_of_targets,
			   (float)config.target_interval / 1000, (float)config.pkt_interval / 1000,
			   (float)config.crit.rta / 1000, (float)max_completion_time / 1000);
	}

	if (debug) {
		if (max_completion_time > (timeout * 1000000)) {
			printf("max_completion_time: %u  timeout: %u\n", max_completion_time, timeout);
			printf("Timeout must be at least %u\n", (max_completion_time / 1000000) + 1);
		}
	}

	if (debug) {
		printf("crit = {%u, %u%%}, warn = {%u, %u%%}\n", config.crit.rta, config.crit.pl,
			   config.warn.rta, config.warn.pl);
		printf("pkt_interval: %u  target_interval: %u\n", config.pkt_interval,
			   config.target_interval);
		printf("icmp_pkt_size: %u  timeout: %u\n", config.icmp_pkt_size, timeout);
	}

	if (config.min_hosts_alive < -1) {
		errno = 0;
		crash("minimum alive hosts is negative (%i)", config.min_hosts_alive);
	}

	ping_target *host = config.targets;
	ping_target **table = malloc(sizeof(ping_target *) * config.number_of_targets);
	if (!table) {
		crash("main(): malloc failed for host table");
	}

	unsigned short target_index = 0;
	while (host) {
		host->id = target_index * config.number_of_packets;
		table[target_index] = host;
		host = host->next;
		target_index++;
	}

	unsigned int pkt_interval = config.pkt_interval;
	unsigned int target_interval = config.target_interval;

	check_icmp_state program_state = check_icmp_state_init();

	run_checks(config.order_mode, config.mos_mode, config.rta_mode, config.pl_mode,
			   config.jitter_mode, config.score_mode, config.min_hosts_alive, config.icmp_data_size,
			   &pkt_interval, &target_interval, config.warn, config.crit, config.sender_id,
			   config.mode, max_completion_time, prog_start, table, config.number_of_packets,
			   icmp_sock, config.number_of_targets, &program_state, config.targets);

	errno = 0;
	finish(0, config.order_mode, config.mos_mode, config.rta_mode, config.pl_mode,
		   config.jitter_mode, config.score_mode, config.min_hosts_alive, config.warn, config.crit,
		   icmp_sock, config.number_of_targets, &program_state, config.targets);

	return (0);
}

static void run_checks(bool order_mode, bool mos_mode, bool rta_mode, bool pl_mode,
					   bool jitter_mode, bool score_mode, int min_hosts_alive,
					   unsigned short icmp_pkt_size, unsigned int *pkt_interval,
					   unsigned int *target_interval, check_icmp_threshold warn,
					   check_icmp_threshold crit, const uint16_t sender_id,
					   const check_icmp_execution_mode mode, const unsigned int max_completion_time,
					   const struct timeval prog_start, ping_target **table,
					   const unsigned short packets, const int icmp_sock,
					   const unsigned short number_of_targets, check_icmp_state *program_state,
					   ping_target *target_list) {
	/* this loop might actually violate the pkt_interval or target_interval
	 * settings, but only if there aren't any packets on the wire which
	 * indicates that the target can handle an increased packet rate */
	for (unsigned int packet_index = 0; packet_index < packets; packet_index++) {
		for (unsigned int target_index = 0; target_index < number_of_targets; target_index++) {
			/* don't send useless packets */
			if (!targets_alive(number_of_targets, program_state->targets_down)) {
				finish(0, order_mode, mos_mode, rta_mode, pl_mode, jitter_mode, score_mode,
					   min_hosts_alive, warn, crit, icmp_sock, number_of_targets, program_state,
					   target_list);
			}
			if (table[target_index]->flags & FLAG_LOST_CAUSE) {
				if (debug) {
					printf("%s is a lost cause. not sending any more\n", table[target_index]->name);
				}
				continue;
			}

			/* we're still in the game, so send next packet */
			(void)send_icmp_ping(icmp_sock, table[target_index], icmp_pkt_size, sender_id,
								 program_state);

			/* wrap up if all targets are declared dead */
			if (targets_alive(number_of_targets, program_state->targets_down) ||
				get_timevaldiff(prog_start, prog_start) < max_completion_time ||
				!(mode == MODE_HOSTCHECK && program_state->targets_down)) {
				wait_for_reply(icmp_sock, *target_interval, icmp_pkt_size, pkt_interval,
							   target_interval, sender_id, table, packets, number_of_targets,
							   program_state);
			}
		}
		if (targets_alive(number_of_targets, program_state->targets_down) ||
			get_timevaldiff_to_now(prog_start) < max_completion_time ||
			!(mode == MODE_HOSTCHECK && program_state->targets_down)) {
			wait_for_reply(icmp_sock, *pkt_interval * number_of_targets, icmp_pkt_size,
						   pkt_interval, target_interval, sender_id, table, packets,
						   number_of_targets, program_state);
		}
	}

	if (icmp_pkts_en_route(program_state->icmp_sent, program_state->icmp_recv,
						   program_state->icmp_lost) &&
		targets_alive(number_of_targets, program_state->targets_down)) {
		time_t time_passed = get_timevaldiff_to_now(prog_start);
		time_t final_wait = max_completion_time - time_passed;

		if (debug) {
			printf("time_passed: %ld  final_wait: %ld  max_completion_time: %u\n", time_passed,
				   final_wait, max_completion_time);
		}
		if (time_passed > max_completion_time) {
			if (debug) {
				printf("Time passed. Finishing up\n");
			}
			finish(0, order_mode, mos_mode, rta_mode, pl_mode, jitter_mode, score_mode,
				   min_hosts_alive, warn, crit, icmp_sock, number_of_targets, program_state,
				   target_list);
		}

		/* catch the packets that might come in within the timeframe, but
		 * haven't yet */
		if (debug) {
			printf("Waiting for %ld micro-seconds (%0.3f msecs)\n", final_wait,
				   (float)final_wait / 1000);
		}
		if (targets_alive(number_of_targets, program_state->targets_down) ||
			get_timevaldiff_to_now(prog_start) < max_completion_time ||
			!(mode == MODE_HOSTCHECK && program_state->targets_down)) {
			wait_for_reply(icmp_sock, final_wait, icmp_pkt_size, pkt_interval, target_interval,
						   sender_id, table, packets, number_of_targets, program_state);
		}
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
static int wait_for_reply(int sock, const time_t time_interval, unsigned short icmp_pkt_size,
						  unsigned int *pkt_interval, unsigned int *target_interval,
						  uint16_t sender_id, ping_target **table, const unsigned short packets,
						  const unsigned short number_of_targets, check_icmp_state *program_state) {
	union icmp_packet packet;
	if (!(packet.buf = malloc(icmp_pkt_size))) {
		crash("send_icmp_ping(): failed to malloc %d bytes for send buffer", icmp_pkt_size);
		return -1; /* might be reached if we're in debug mode */
	}

	memset(packet.buf, 0, icmp_pkt_size);

	/* if we can't listen or don't have anything to listen to, just return */
	if (!time_interval || !icmp_pkts_en_route(program_state->icmp_sent, program_state->icmp_recv,
											  program_state->icmp_lost)) {
		free(packet.buf);
		return 0;
	}

	// Get current time stamp
	struct timeval wait_start;
	gettimeofday(&wait_start, NULL);

	struct sockaddr_storage resp_addr;
	time_t per_pkt_wait =
		time_interval / icmp_pkts_en_route(program_state->icmp_sent, program_state->icmp_recv,
										   program_state->icmp_lost);
	static unsigned char buf[65536];
	union ip_hdr *ip_header;
	struct timeval packet_received_timestamp;
	while (icmp_pkts_en_route(program_state->icmp_sent, program_state->icmp_recv,
							  program_state->icmp_lost) &&
		   get_timevaldiff_to_now(wait_start) < time_interval) {
		time_t loop_time_interval = per_pkt_wait;

		/* reap responses until we hit a timeout */
		ssize_t n = recvfrom_wto(sock, buf, sizeof(buf), (struct sockaddr *)&resp_addr,
								 &loop_time_interval, &packet_received_timestamp);
		if (!n) {
			if (debug > 1) {
				printf("recvfrom_wto() timed out during a %ld usecs wait\n", per_pkt_wait);
			}
			continue; /* timeout for this one, so keep trying */
		}

		if (n < 0) {
			if (debug) {
				printf("recvfrom_wto() returned errors\n");
			}
			free(packet.buf);
			return (int)n;
		}

		// FIXME: with ipv6 we don't have an ip header here
		if (address_family != AF_INET6) {
			ip_header = (union ip_hdr *)buf;

			if (debug > 1) {
				char address[INET6_ADDRSTRLEN];
				parse_address(&resp_addr, address, sizeof(address));
				printf("received %u bytes from %s\n",
					   address_family == AF_INET6 ? ntohs(ip_header->ip6.ip6_plen)
												  : ntohs(ip_header->ip.ip_len),
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
		int hlen = (address_family == AF_INET6) ? 0 : ip_header->ip.ip_hl << 2;
		/* #endif */

		if (n < (hlen + ICMP_MINLEN)) {
			char address[INET6_ADDRSTRLEN];
			parse_address(&resp_addr, address, sizeof(address));
			crash("received packet too short for ICMP (%d bytes, expected %d) from %s\n", n,
				  hlen + icmp_pkt_size, address);
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

		if ((address_family == PF_INET &&
			 (ntohs(packet.icp->icmp_id) != sender_id || packet.icp->icmp_type != ICMP_ECHOREPLY ||
			  ntohs(packet.icp->icmp_seq) >= number_of_targets * packets)) ||
			(address_family == PF_INET6 &&
			 (ntohs(packet.icp6->icmp6_id) != sender_id ||
			  packet.icp6->icmp6_type != ICMP6_ECHO_REPLY ||
			  ntohs(packet.icp6->icmp6_seq) >= number_of_targets * packets))) {
			if (debug > 2) {
				printf("not a proper ICMP_ECHOREPLY\n");
			}

			handle_random_icmp(buf + hlen, &resp_addr, pkt_interval, target_interval, sender_id,
							   table, packets, number_of_targets, program_state);

			continue;
		}

		/* this is indeed a valid response */
		ping_target *target;
		struct icmp_ping_data data;
		if (address_family == PF_INET) {
			memcpy(&data, packet.icp->icmp_data, sizeof(data));
			if (debug > 2) {
				printf("ICMP echo-reply of len %lu, id %u, seq %u, cksum 0x%X\n", sizeof(data),
					   ntohs(packet.icp->icmp_id), ntohs(packet.icp->icmp_seq),
					   packet.icp->icmp_cksum);
			}
			target = table[ntohs(packet.icp->icmp_seq) / packets];
		} else {
			memcpy(&data, &packet.icp6->icmp6_dataun.icmp6_un_data8[4], sizeof(data));
			if (debug > 2) {
				printf("ICMP echo-reply of len %lu, id %u, seq %u, cksum 0x%X\n", sizeof(data),
					   ntohs(packet.icp6->icmp6_id), ntohs(packet.icp6->icmp6_seq),
					   packet.icp6->icmp6_cksum);
			}
			target = table[ntohs(packet.icp6->icmp6_seq) / packets];
		}

		time_t tdiff = get_timevaldiff(data.stime, packet_received_timestamp);

		if (target->last_tdiff > 0) {
			/* Calculate jitter */
			double jitter_tmp;
			if (target->last_tdiff > tdiff) {
				jitter_tmp = (double)(target->last_tdiff - tdiff);
			} else {
				jitter_tmp = (double)(tdiff - target->last_tdiff);
			}

			if (target->jitter == 0) {
				target->jitter = jitter_tmp;
				target->jitter_max = jitter_tmp;
				target->jitter_min = jitter_tmp;
			} else {
				target->jitter += jitter_tmp;

				if (jitter_tmp < target->jitter_min) {
					target->jitter_min = jitter_tmp;
				}

				if (jitter_tmp > target->jitter_max) {
					target->jitter_max = jitter_tmp;
				}
			}

			/* Check if packets in order */
			if (target->last_icmp_seq >= packet.icp->icmp_seq) {
				target->order_status = STATE_CRITICAL;
			}
		}
		target->last_tdiff = tdiff;

		target->last_icmp_seq = packet.icp->icmp_seq;

		target->time_waited += tdiff;
		target->icmp_recv++;
		program_state->icmp_recv++;

		if (tdiff > (unsigned int)target->rtmax) {
			target->rtmax = (double)tdiff;
		}

		if ((target->rtmin == INFINITY) || (tdiff < (unsigned int)target->rtmin)) {
			target->rtmin = (double)tdiff;
		}

		if (debug) {
			char address[INET6_ADDRSTRLEN];
			parse_address(&resp_addr, address, sizeof(address));

			switch (address_family) {
			case AF_INET: {
				printf("%0.3f ms rtt from %s, incoming ttl: %u, max: %0.3f, min: %0.3f\n",
					   (float)tdiff / 1000, address, ip_header->ip.ip_ttl,
					   (float)target->rtmax / 1000, (float)target->rtmin / 1000);
				break;
			};
			case AF_INET6: {
				printf("%0.3f ms rtt from %s, max: %0.3f, min: %0.3f\n", (float)tdiff / 1000,
					   address, (float)target->rtmax / 1000, (float)target->rtmin / 1000);
			};
			}
		}
	}

	free(packet.buf);
	return 0;
}

/* the ping functions */
static int send_icmp_ping(const int sock, ping_target *host, const unsigned short icmp_pkt_size,
						  const uint16_t sender_id, check_icmp_state *program_state) {
	if (sock == -1) {
		errno = 0;
		crash("Attempt to send on bogus socket");
		return -1;
	}

	void *buf = NULL;

	if (!buf) {
		if (!(buf = malloc(icmp_pkt_size))) {
			crash("send_icmp_ping(): failed to malloc %d bytes for send buffer", icmp_pkt_size);
			return -1; /* might be reached if we're in debug mode */
		}
	}
	memset(buf, 0, icmp_pkt_size);

	struct timeval current_time;
	if ((gettimeofday(&current_time, NULL)) == -1) {
		free(buf);
		return -1;
	}

	struct icmp_ping_data data;
	data.ping_id = 10; /* host->icmp.icmp_sent; */
	memcpy(&data.stime, &current_time, sizeof(current_time));

	socklen_t addrlen;

	if (address_family == AF_INET) {
		struct icmp *icp = (struct icmp *)buf;
		addrlen = sizeof(struct sockaddr_in);

		memcpy(&icp->icmp_data, &data, sizeof(data));

		icp->icmp_type = ICMP_ECHO;
		icp->icmp_code = 0;
		icp->icmp_cksum = 0;
		icp->icmp_id = htons((uint16_t)sender_id);
		icp->icmp_seq = htons(host->id++);
		icp->icmp_cksum = icmp_checksum((uint16_t *)buf, (size_t)icmp_pkt_size);

		if (debug > 2) {
			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to host %s\n",
				   sizeof(data), ntohs(icp->icmp_id), ntohs(icp->icmp_seq), icp->icmp_cksum,
				   host->name);
		}
	} else {
		struct icmp6_hdr *icp6 = (struct icmp6_hdr *)buf;
		addrlen = sizeof(struct sockaddr_in6);

		memcpy(&icp6->icmp6_dataun.icmp6_un_data8[4], &data, sizeof(data));

		icp6->icmp6_type = ICMP6_ECHO_REQUEST;
		icp6->icmp6_code = 0;
		icp6->icmp6_cksum = 0;
		icp6->icmp6_id = htons((uint16_t)sender_id);
		icp6->icmp6_seq = htons(host->id++);
		// let checksum be calculated automatically

		if (debug > 2) {
			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to host %s\n",
				   sizeof(data), ntohs(icp6->icmp6_id), ntohs(icp6->icmp6_seq), icp6->icmp6_cksum,
				   host->name);
		}
	}

	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = icmp_pkt_size;

	struct msghdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = (struct sockaddr *)&host->saddr_in;
	hdr.msg_namelen = addrlen;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;

	errno = 0;

	long int len;
/* MSG_CONFIRM is a linux thing and only available on linux kernels >= 2.3.15, see send(2) */
#ifdef MSG_CONFIRM
	len = sendmsg(sock, &hdr, MSG_CONFIRM);
#else
	len = sendmsg(sock, &hdr, 0);
#endif

	free(buf);

	if (len < 0 || (unsigned int)len != icmp_pkt_size) {
		if (debug) {
			char address[INET6_ADDRSTRLEN];
			parse_address((&host->saddr_in), address, sizeof(address));
			printf("Failed to send ping to %s: %s\n", address, strerror(errno));
		}
		errno = 0;
		return -1;
	}

	program_state->icmp_sent++;
	host->icmp_sent++;

	return 0;
}

static ssize_t recvfrom_wto(const int sock, void *buf, const unsigned int len,
							struct sockaddr *saddr, time_t *timeout,
							struct timeval *received_timestamp) {
#ifdef HAVE_MSGHDR_MSG_CONTROL
	char ans_data[4096];
#endif // HAVE_MSGHDR_MSG_CONTROL
#ifdef SO_TIMESTAMP
	struct cmsghdr *chdr;
#endif

	if (!*timeout) {
		if (debug) {
			printf("*timeout is not\n");
		}
		return 0;
	}

	struct timeval real_timeout;
	real_timeout.tv_sec = *timeout / 1000000;
	real_timeout.tv_usec = (*timeout - (real_timeout.tv_sec * 1000000));

	// Dummy fds for select
	fd_set dummy_write_fds;
	FD_ZERO(&dummy_write_fds);

	// Read fds for select with the socket
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(sock, &read_fds);

	struct timeval then;
	gettimeofday(&then, NULL);

	errno = 0;
	int select_return = select(sock + 1, &read_fds, &dummy_write_fds, NULL, &real_timeout);
	if (select_return < 0) {
		crash("select() in recvfrom_wto");
	}

	struct timeval now;
	gettimeofday(&now, NULL);
	*timeout = get_timevaldiff(then, now);

	if (!select_return) {
		return 0; /* timeout */
	}

	unsigned int slen = sizeof(struct sockaddr_storage);

	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};

	struct msghdr hdr = {
		.msg_name = saddr,
		.msg_namelen = slen,
		.msg_iov = &iov,
		.msg_iovlen = 1,
#ifdef HAVE_MSGHDR_MSG_CONTROL
		.msg_control = ans_data,
		.msg_controllen = sizeof(ans_data),
#endif
	};

	ssize_t ret = recvmsg(sock, &hdr, 0);

#ifdef SO_TIMESTAMP
	for (chdr = CMSG_FIRSTHDR(&hdr); chdr; chdr = CMSG_NXTHDR(&hdr, chdr)) {
		if (chdr->cmsg_level == SOL_SOCKET && chdr->cmsg_type == SO_TIMESTAMP &&
			chdr->cmsg_len >= CMSG_LEN(sizeof(struct timeval))) {
			memcpy(received_timestamp, CMSG_DATA(chdr), sizeof(*received_timestamp));
			break;
		}
	}

	if (!chdr) {
		gettimeofday(received_timestamp, NULL);
	}
#else
	gettimeofday(tv, NULL);
#endif // SO_TIMESTAMP

	return (ret);
}

static void finish(int sig, bool order_mode, bool mos_mode, bool rta_mode, bool pl_mode,
				   bool jitter_mode, bool score_mode, int min_hosts_alive,
				   check_icmp_threshold warn, check_icmp_threshold crit, const int icmp_sock,
				   const unsigned short number_of_targets, check_icmp_state *program_state,
				   ping_target *target_list) {
	// Deactivate alarm
	alarm(0);

	if (debug > 1) {
		printf("finish(%d) called\n", sig);
	}

	if (icmp_sock != -1) {
		close(icmp_sock);
	}

	if (debug) {
		printf("icmp_sent: %u  icmp_recv: %u  icmp_lost: %u\n", program_state->icmp_sent,
			   program_state->icmp_recv, program_state->icmp_lost);
		printf("targets: %u  targets_alive: %u\n", number_of_targets,
			   targets_alive(number_of_targets, program_state->targets_down));
	}

	/* iterate thrice to calculate values, give output, and print perfparse */
	mp_state_enum status = STATE_OK;
	ping_target *host = target_list;

	unsigned int target_counter = 0;
	const char *status_string[] = {"OK", "WARNING", "CRITICAL", "UNKNOWN", "DEPENDENT"};
	int hosts_ok = 0;
	int hosts_warn = 0;
	while (host) {
		mp_state_enum this_status = STATE_OK;

		unsigned char packet_loss;
		double rta;
		if (!host->icmp_recv) {
			/* rta 0 is ofcourse not entirely correct, but will still show up
			 * conspicuously as missing entries in perfparse and cacti */
			packet_loss = 100;
			rta = 0;
			status = STATE_CRITICAL;
			/* up the down counter if not already counted */
			if (!(host->flags & FLAG_LOST_CAUSE) &&
				targets_alive(number_of_targets, program_state->targets_down)) {
				program_state->targets_down++;
			}
		} else {
			packet_loss =
				(unsigned char)((host->icmp_sent - host->icmp_recv) * 100) / host->icmp_sent;
			rta = (double)host->time_waited / host->icmp_recv;
		}

		if (host->icmp_recv > 1) {
			/*
			 * This algorithm is probably pretty much blindly copied from
			 * locations like this one:
			 * https://www.slac.stanford.edu/comp/net/wan-mon/tutorial.html#mos It calculates a MOS
			 * value (range of 1 to 5, where 1 is bad and 5 really good). According to some quick
			 * research MOS originates from the Audio/Video transport network area. Whether it can
			 * and should be computed from ICMP data, I can not say.
			 *
			 * Anyway the basic idea is to map a value "R" with a range of 0-100 to the MOS value
			 *
			 * MOS stands likely for Mean Opinion Score (
			 * https://en.wikipedia.org/wiki/Mean_Opinion_Score )
			 *
			 * More links:
			 * - https://confluence.slac.stanford.edu/display/IEPM/MOS
			 */
			host->jitter = (host->jitter / (host->icmp_recv - 1) / 1000);

			/*
			 * Take the average round trip latency (in milliseconds), add
			 * round trip jitter, but double the impact to latency
			 * then add 10 for protocol latencies (in milliseconds).
			 */
			host->EffectiveLatency = (rta / 1000) + host->jitter * 2 + 10;

			double R;
			if (host->EffectiveLatency < 160) {
				R = 93.2 - (host->EffectiveLatency / 40);
			} else {
				R = 93.2 - ((host->EffectiveLatency - 120) / 10);
			}

			// Now, let us deduct 2.5 R values per percentage of packet loss (i.e. a
			// loss of 5% will be entered as 5).
			R = R - (packet_loss * 2.5);

			if (R < 0) {
				R = 0;
			}

			host->score = R;
			host->mos = 1 + ((0.035) * R) + ((.000007) * R * (R - 60) * (100 - R));
		} else {
			host->jitter = 0;
			host->jitter_min = 0;
			host->jitter_max = 0;
			host->mos = 0;
		}

		host->pl = packet_loss;
		host->rta = rta;

		/* if no new mode selected, use old schema */
		if (!rta_mode && !pl_mode && !jitter_mode && !score_mode && !mos_mode && !order_mode) {
			rta_mode = true;
			pl_mode = true;
		}

		/* Check which mode is on and do the warn / Crit stuff */
		if (rta_mode) {
			if (rta >= crit.rta) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->rta_status = STATE_CRITICAL;
			} else if (status != STATE_CRITICAL && (rta >= warn.rta)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->rta_status = STATE_WARNING;
			}
		}

		if (pl_mode) {
			if (packet_loss >= crit.pl) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->pl_status = STATE_CRITICAL;
			} else if (status != STATE_CRITICAL && (packet_loss >= warn.pl)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->pl_status = STATE_WARNING;
			}
		}

		if (jitter_mode) {
			if (host->jitter >= crit.jitter) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->jitter_status = STATE_CRITICAL;
			} else if (status != STATE_CRITICAL && (host->jitter >= warn.jitter)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->jitter_status = STATE_WARNING;
			}
		}

		if (mos_mode) {
			if (host->mos <= crit.mos) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->mos_status = STATE_CRITICAL;
			} else if (status != STATE_CRITICAL && (host->mos <= warn.mos)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->mos_status = STATE_WARNING;
			}
		}

		if (score_mode) {
			if (host->score <= crit.score) {
				this_status = STATE_CRITICAL;
				status = STATE_CRITICAL;
				host->score_status = STATE_CRITICAL;
			} else if (status != STATE_CRITICAL && (host->score <= warn.score)) {
				this_status = (this_status <= STATE_WARNING ? STATE_WARNING : this_status);
				status = STATE_WARNING;
				host->score_status = STATE_WARNING;
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
	if (!targets_alive(number_of_targets, program_state->targets_down)) {
		status = STATE_CRITICAL;
	}
	if (min_hosts_alive > -1) {
		if (hosts_ok >= min_hosts_alive) {
			status = STATE_OK;
		} else if ((hosts_ok + hosts_warn) >= min_hosts_alive) {
			status = STATE_WARNING;
		}
	}
	printf("%s - ", status_string[status]);

	host = target_list;
	while (host) {
		if (debug) {
			puts("");
		}

		if (target_counter) {
			if (target_counter < number_of_targets) {
				printf(" :: ");
			} else {
				printf("\n");
			}
		}

		target_counter++;

		if (!host->icmp_recv) {
			status = STATE_CRITICAL;
			host->rtmin = 0;
			host->jitter_min = 0;

			if (host->flags & FLAG_LOST_CAUSE) {
				char address[INET6_ADDRSTRLEN];
				parse_address(&host->error_addr, address, sizeof(address));
				printf("%s: %s @ %s. rta nan, lost %d%%", host->name,
					   get_icmp_error_msg(host->icmp_type, host->icmp_code), address, 100);
			} else { /* not marked as lost cause, so we have no flags for it */
				printf("%s: rta nan, lost 100%%", host->name);
			}
		} else { /* !icmp_recv */
			printf("%s", host->name);
			/* rta text output */
			if (rta_mode) {
				if (status == STATE_OK) {
					printf(" rta %0.3fms", host->rta / 1000);
				} else if (status == STATE_WARNING && host->rta_status == status) {
					printf(" rta %0.3fms > %0.3fms", (float)host->rta / 1000,
						   (float)warn.rta / 1000);
				} else if (status == STATE_CRITICAL && host->rta_status == status) {
					printf(" rta %0.3fms > %0.3fms", (float)host->rta / 1000,
						   (float)crit.rta / 1000);
				}
			}

			/* pl text output */
			if (pl_mode) {
				if (status == STATE_OK) {
					printf(" lost %u%%", host->pl);
				} else if (status == STATE_WARNING && host->pl_status == status) {
					printf(" lost %u%% > %u%%", host->pl, warn.pl);
				} else if (status == STATE_CRITICAL && host->pl_status == status) {
					printf(" lost %u%% > %u%%", host->pl, crit.pl);
				}
			}

			/* jitter text output */
			if (jitter_mode) {
				if (status == STATE_OK) {
					printf(" jitter %0.3fms", (float)host->jitter);
				} else if (status == STATE_WARNING && host->jitter_status == status) {
					printf(" jitter %0.3fms > %0.3fms", (float)host->jitter, warn.jitter);
				} else if (status == STATE_CRITICAL && host->jitter_status == status) {
					printf(" jitter %0.3fms > %0.3fms", (float)host->jitter, crit.jitter);
				}
			}

			/* mos text output */
			if (mos_mode) {
				if (status == STATE_OK) {
					printf(" MOS %0.1f", (float)host->mos);
				} else if (status == STATE_WARNING && host->mos_status == status) {
					printf(" MOS %0.1f < %0.1f", (float)host->mos, (float)warn.mos);
				} else if (status == STATE_CRITICAL && host->mos_status == status) {
					printf(" MOS %0.1f < %0.1f", (float)host->mos, (float)crit.mos);
				}
			}

			/* score text output */
			if (score_mode) {
				if (status == STATE_OK) {
					printf(" Score %u", (int)host->score);
				} else if (status == STATE_WARNING && host->score_status == status) {
					printf(" Score %u < %u", (int)host->score, (int)warn.score);
				} else if (status == STATE_CRITICAL && host->score_status == status) {
					printf(" Score %u < %u", (int)host->score, (int)crit.score);
				}
			}

			/* order statis text output */
			if (order_mode) {
				if (status == STATE_OK) {
					printf(" Packets in order");
				} else if (status == STATE_CRITICAL && host->order_status == status) {
					printf(" Packets out of order");
				}
			}
		}
		host = host->next;
	}

	/* iterate once more for pretty perfparse output */
	if (!(!rta_mode && !pl_mode && !jitter_mode && !score_mode && !mos_mode && order_mode)) {
		printf("|");
	}

	target_counter = 0;
	host = target_list;
	while (host) {
		if (debug) {
			puts("");
		}

		if (rta_mode) {
			if (host->pl < 100) {
				printf("%srta=%0.3fms;%0.3f;%0.3f;0; %srtmax=%0.3fms;;;; %srtmin=%0.3fms;;;; ",
					   (number_of_targets > 1) ? host->name : "", host->rta / 1000,
					   (float)warn.rta / 1000, (float)crit.rta / 1000,
					   (number_of_targets > 1) ? host->name : "", (float)host->rtmax / 1000,
					   (number_of_targets > 1) ? host->name : "",
					   (host->rtmin < INFINITY) ? (float)host->rtmin / 1000 : (float)0);
			} else {
				printf("%srta=U;;;; %srtmax=U;;;; %srtmin=U;;;; ",
					   (number_of_targets > 1) ? host->name : "",
					   (number_of_targets > 1) ? host->name : "",
					   (number_of_targets > 1) ? host->name : "");
			}
		}

		if (pl_mode) {
			printf("%spl=%u%%;%u;%u;0;100 ", (number_of_targets > 1) ? host->name : "", host->pl,
				   warn.pl, crit.pl);
		}

		if (jitter_mode) {
			if (host->pl < 100) {
				printf("%sjitter_avg=%0.3fms;%0.3f;%0.3f;0; %sjitter_max=%0.3fms;;;; "
					   "%sjitter_min=%0.3fms;;;; ",
					   (number_of_targets > 1) ? host->name : "", (float)host->jitter,
					   (float)warn.jitter, (float)crit.jitter,
					   (number_of_targets > 1) ? host->name : "", (float)host->jitter_max / 1000,
					   (number_of_targets > 1) ? host->name : "", (float)host->jitter_min / 1000);
			} else {
				printf("%sjitter_avg=U;;;; %sjitter_max=U;;;; %sjitter_min=U;;;; ",
					   (number_of_targets > 1) ? host->name : "",
					   (number_of_targets > 1) ? host->name : "",
					   (number_of_targets > 1) ? host->name : "");
			}
		}

		if (mos_mode) {
			if (host->pl < 100) {
				printf("%smos=%0.1f;%0.1f;%0.1f;0;5 ", (number_of_targets > 1) ? host->name : "",
					   (float)host->mos, (float)warn.mos, (float)crit.mos);
			} else {
				printf("%smos=U;;;; ", (number_of_targets > 1) ? host->name : "");
			}
		}

		if (score_mode) {
			if (host->pl < 100) {
				printf("%sscore=%u;%u;%u;0;100 ", (number_of_targets > 1) ? host->name : "",
					   (int)host->score, (int)warn.score, (int)crit.score);
			} else {
				printf("%sscore=U;;;; ", (number_of_targets > 1) ? host->name : "");
			}
		}

		host = host->next;
	}

	if (min_hosts_alive > -1) {
		if (hosts_ok >= min_hosts_alive) {
			status = STATE_OK;
		} else if ((hosts_ok + hosts_warn) >= min_hosts_alive) {
			status = STATE_WARNING;
		}
	}

	/* finish with an empty line */
	puts("");
	if (debug) {
		printf(
			"targets: %u, targets_alive: %u, hosts_ok: %u, hosts_warn: %u, min_hosts_alive: %i\n",
			number_of_targets, targets_alive(number_of_targets, program_state->targets_down),
			hosts_ok, hosts_warn, min_hosts_alive);
	}

	exit(status);
}

static time_t get_timevaldiff(const struct timeval earlier, const struct timeval later) {
	/* if early > later we return 0 so as to indicate a timeout */
	if (earlier.tv_sec > later.tv_sec ||
		(earlier.tv_sec == later.tv_sec && earlier.tv_usec > later.tv_usec)) {
		return 0;
	}

	time_t ret = (later.tv_sec - earlier.tv_sec) * 1000000;
	ret += later.tv_usec - earlier.tv_usec;

	return ret;
}

static time_t get_timevaldiff_to_now(struct timeval earlier) {
	struct timeval now;
	gettimeofday(&now, NULL);

	return get_timevaldiff(earlier, now);
}

static add_target_ip_wrapper add_target_ip(char *arg, struct sockaddr_storage *address) {
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	if (address_family == AF_INET) {
		sin = (struct sockaddr_in *)address;
	} else {
		sin6 = (struct sockaddr_in6 *)address;
	}

	add_target_ip_wrapper result = {
		.error_code = OK,
		.target = NULL,
	};

	/* disregard obviously stupid addresses
	 * (I didn't find an ipv6 equivalent to INADDR_NONE) */
	if (((address_family == AF_INET &&
		  (sin->sin_addr.s_addr == INADDR_NONE || sin->sin_addr.s_addr == INADDR_ANY))) ||
		(address_family == AF_INET6 && (sin6->sin6_addr.s6_addr == in6addr_any.s6_addr))) {
		result.error_code = ERROR;
		return result;
	}

	// TODO: allow duplicate targets for now, might be on purpose
	/* no point in adding two identical IP's, so don't. ;) */
	// struct sockaddr_in *host_sin;
	// struct sockaddr_in6 *host_sin6;
	// ping_target *host = host_list;
	// while (host) {
	// 	host_sin = (struct sockaddr_in *)&host->saddr_in;
	// 	host_sin6 = (struct sockaddr_in6 *)&host->saddr_in;

	// 	if ((address_family == AF_INET && host_sin->sin_addr.s_addr == sin->sin_addr.s_addr) ||
	// 		(address_family == AF_INET6 &&
	// 		 host_sin6->sin6_addr.s6_addr == sin6->sin6_addr.s6_addr)) {
	// 		if (debug) {
	// 			printf("Identical IP already exists. Not adding %s\n", arg);
	// 		}
	// 		return -1;
	// 	}
	// 	host = host->next;
	// }

	/* add the fresh ip */
	ping_target *target = (ping_target *)malloc(sizeof(ping_target));
	if (!target) {
		char straddr[INET6_ADDRSTRLEN];
		parse_address((struct sockaddr_storage *)&address, straddr, sizeof(straddr));
		crash("add_target_ip(%s, %s): malloc(%lu) failed", arg, straddr, sizeof(ping_target));
	}

	*target = ping_target_init();

	/* set the values. use calling name for output */
	target->name = strdup(arg);

	/* fill out the sockaddr_storage struct */
	struct sockaddr_in *host_sin;
	struct sockaddr_in6 *host_sin6;
	if (address_family == AF_INET) {
		host_sin = (struct sockaddr_in *)&target->saddr_in;
		host_sin->sin_family = AF_INET;
		host_sin->sin_addr.s_addr = sin->sin_addr.s_addr;
	} else {
		host_sin6 = (struct sockaddr_in6 *)&target->saddr_in;
		host_sin6->sin6_family = AF_INET6;
		memcpy(host_sin6->sin6_addr.s6_addr, sin6->sin6_addr.s6_addr,
			   sizeof host_sin6->sin6_addr.s6_addr);
	}

	result.target = target;

	return result;
}

/* wrapper for add_target_ip */
static add_target_wrapper add_target(char *arg, const check_icmp_execution_mode mode) {
	struct sockaddr_storage address_storage;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int error_code = -1;

	switch (address_family) {
	case -1:
		/* -4 and -6 are not specified on cmdline */
		address_family = AF_INET;
		sin = (struct sockaddr_in *)&address_storage;
		error_code = inet_pton(address_family, arg, &sin->sin_addr);

		if (error_code != 1) {
			address_family = AF_INET6;
			sin6 = (struct sockaddr_in6 *)&address_storage;
			error_code = inet_pton(address_family, arg, &sin6->sin6_addr);
		}
		/* If we don't find any valid addresses, we still don't know the address_family */
		if (error_code != 1) {
			address_family = -1;
		}
		break;
	case AF_INET:
		sin = (struct sockaddr_in *)&address_storage;
		error_code = inet_pton(address_family, arg, &sin->sin_addr);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&address_storage;
		error_code = inet_pton(address_family, arg, &sin6->sin6_addr);
		break;
	default:
		crash("Address family not supported");
	}

	add_target_wrapper result = {
		.error_code = OK,
		.targets = NULL,
	};

	/* don't resolve if we don't have to */
	if (error_code == 1) {
		/* don't add all ip's if we were given a specific one */
		add_target_ip_wrapper targeted = add_target_ip(arg, &address_storage);

		if (targeted.error_code != OK) {
			result.error_code = ERROR;
			return result;
		}

		result.targets = targeted.target;
		result.number_of_targets = 1;
		return result;
	}

	struct addrinfo hints;
	errno = 0;
	memset(&hints, 0, sizeof(hints));
	if (address_family == -1) {
		hints.ai_family = AF_UNSPEC;
	} else {
		hints.ai_family = address_family == AF_INET ? PF_INET : PF_INET6;
	}
	hints.ai_socktype = SOCK_RAW;

	int error;
	struct addrinfo *res;
	if ((error = getaddrinfo(arg, NULL, &hints, &res)) != 0) {
		errno = 0;
		crash("Failed to resolve %s: %s", arg, gai_strerror(error));
		result.error_code = ERROR;
		return result;
	}
	address_family = res->ai_family;

	/* possibly add all the IP's as targets */
	for (struct addrinfo *address = res; address != NULL; address = address->ai_next) {
		struct sockaddr_storage temporary_ip_address;
		memcpy(&temporary_ip_address, address->ai_addr, address->ai_addrlen);
		add_target_ip_wrapper tmp = add_target_ip(arg, &temporary_ip_address);

		if (tmp.error_code != OK) {
			// No proper error handling
			// What to do?
		} else {
			if (result.targets == NULL) {
				result.targets = tmp.target;
				result.number_of_targets = 1;
			} else {
				result.number_of_targets += ping_target_list_append(result.targets, tmp.target);
			}
		}

		/* this is silly, but it works */
		if (mode == MODE_HOSTCHECK || mode == MODE_ALL) {
			if (debug > 2) {
				printf("mode: %d\n", mode);
			}
			continue;
		}

		// Abort after first hit if not in of the modes above
		break;
	}
	freeaddrinfo(res);

	return result;
}

static void set_source_ip(char *arg, const int icmp_sock) {
	struct sockaddr_in src;

	memset(&src, 0, sizeof(src));
	src.sin_family = address_family;
	if ((src.sin_addr.s_addr = inet_addr(arg)) == INADDR_NONE) {
		src.sin_addr.s_addr = get_ip_address(arg);
	}
	if (bind(icmp_sock, (struct sockaddr *)&src, sizeof(src)) == -1) {
		crash("Cannot bind to IP address %s", arg);
	}
}

/* TODO: Move this to netutils.c and also change check_dhcp to use that. */
static in_addr_t get_ip_address(const char *ifname) {
	// TODO: Rewrite this so the function return an error and we exit somewhere else
	struct sockaddr_in ip_address;
	ip_address.sin_addr.s_addr = 0; // Fake initialization to make compiler happy
#if defined(SIOCGIFADDR)
	struct ifreq ifr;

	strncpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name) - 1);

	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = '\0';

	if (ioctl(icmp_sock, SIOCGIFADDR, &ifr) == -1) {
		crash("Cannot determine IP address of interface %s", ifname);
	}

	memcpy(&ip, &ifr.ifr_addr, sizeof(ip));
#else
	(void)ifname;
	errno = 0;
	crash("Cannot get interface IP address on this platform.");
#endif
	return ip_address.sin_addr.s_addr;
}

/*
 * u = micro
 * m = milli
 * s = seconds
 * return value is in microseconds
 */
static unsigned int get_timevar(const char *str) {
	if (!str) {
		return 0;
	}

	size_t len = strlen(str);
	if (!len) {
		return 0;
	}

	/* unit might be given as ms|m (millisec),
	 * us|u (microsec) or just plain s, for seconds */
	char tmp = '\0';
	char unit = str[len - 1];
	if (len >= 2 && !isdigit((int)str[len - 2])) {
		tmp = str[len - 2];
	}

	if (tmp && unit == 's') {
		unit = tmp;
	} else if (!tmp) {
		tmp = unit;
	}

	if (debug > 2) {
		printf("evaluating %s, u: %c, p: %c\n", str, unit, tmp);
	}

	unsigned int factor = 1000; /* default to milliseconds */
	if (unit == 'u') {
		factor = 1; /* microseconds */
	} else if (unit == 'm') {
		factor = 1000; /* milliseconds */
	} else if (unit == 's') {
		factor = 1000000; /* seconds */
	}

	if (debug > 2) {
		printf("factor is %u\n", factor);
	}

	char *ptr;
	unsigned long pre_radix;
	pre_radix = strtoul(str, &ptr, 0);
	if (!ptr || *ptr != '.' || strlen(ptr) < 2 || factor == 1) {
		return (unsigned int)(pre_radix * factor);
	}

	/* time specified in usecs can't have decimal points, so ignore them */
	if (factor == 1) {
		return (unsigned int)pre_radix;
	}

	/* integer and decimal, respectively */
	unsigned int post_radix = (unsigned int)strtoul(ptr + 1, NULL, 0);

	/* d is decimal, so get rid of excess digits */
	while (post_radix >= factor) {
		post_radix /= 10;
	}

	/* the last parenthesis avoids floating point exceptions. */
	return (unsigned int)((pre_radix * factor) + (post_radix * (factor / 10)));
}

/* not too good at checking errors, but it'll do (main() should barfe on -1) */
static get_threshold_wrapper get_threshold(char *str, check_icmp_threshold threshold) {
	get_threshold_wrapper result = {
		.errorcode = OK,
		.threshold = threshold,
	};

	if (!str || !strlen(str)) {
		result.errorcode = ERROR;
		return result;
	}

	/* pointer magic slims code by 10 lines. i is bof-stop on stupid libc's */
	bool is_at_last_char = false;
	char *tmp = &str[strlen(str) - 1];
	while (tmp != &str[1]) {
		if (*tmp == '%') {
			*tmp = '\0';
		} else if (*tmp == ',' && is_at_last_char) {
			*tmp = '\0'; /* reset it so get_timevar(str) works nicely later */
			result.threshold.pl = (unsigned char)strtoul(tmp + 1, NULL, 0);
			break;
		}
		is_at_last_char = true;
		tmp--;
	}
	result.threshold.rta = get_timevar(str);

	if (!result.threshold.rta) {
		result.errorcode = ERROR;
		return result;
	}

	if (result.threshold.rta > MAXTTL * 1000000) {
		result.threshold.rta = MAXTTL * 1000000;
	}
	if (result.threshold.pl > 100) {
		result.threshold.pl = 100;
	}

	return result;
}

/*
 * This functions receives a pointer to a string which should contain a threshold for the
 * rta, packet_loss, jitter, mos or score mode in the form number,number[m|%]* assigns the
 * parsed number to the corresponding threshold variable.
 * @param[in,out] str String containing the given threshold values
 * @param[in] length strlen(str)
 * @param[out] warn Pointer to the warn threshold struct to which the values should be assigned
 * @param[out] crit Pointer to the crit threshold struct to which the values should be assigned
 * @param[in] mode Determines whether this a threshold for rta, packet_loss, jitter, mos or score
 * (exclusively)
 */
static get_threshold2_wrapper get_threshold2(char *str, size_t length, check_icmp_threshold warn,
											 check_icmp_threshold crit, threshold_mode mode) {
	get_threshold2_wrapper result = {
		.errorcode = OK,
		.warn = warn,
		.crit = crit,
	};

	if (!str || !length) {
		result.errorcode = ERROR;
		return result;
	}

	// p points to the last char in str
	char *work_pointer = &str[length - 1];

	// first_iteration is bof-stop on stupid libc's
	bool first_iteration = true;

	while (work_pointer != &str[0]) {
		if ((*work_pointer == 'm') || (*work_pointer == '%')) {
			*work_pointer = '\0';
		} else if (*work_pointer == ',' && !first_iteration) {
			*work_pointer = '\0'; /* reset it so get_timevar(str) works nicely later */

			char *start_of_value = work_pointer + 1;

			parse_threshold2_helper_wrapper tmp =
				parse_threshold2_helper(start_of_value, strlen(start_of_value), result.crit, mode);
			if (tmp.errorcode != OK) {
				result.errorcode = ERROR;
				return result;
			}
			result.crit = tmp.result;
		}
		first_iteration = false;
		work_pointer--;
	}

	parse_threshold2_helper_wrapper tmp =
		parse_threshold2_helper(work_pointer, strlen(work_pointer), result.warn, mode);
	if (tmp.errorcode != OK) {
		result.errorcode = ERROR;
	} else {
		result.warn = tmp.result;
	}
	return result;
}

static parse_threshold2_helper_wrapper parse_threshold2_helper(char *threshold_string,
															   size_t length,
															   check_icmp_threshold thr,
															   threshold_mode mode) {
	char *resultChecker = {0};
	parse_threshold2_helper_wrapper result = {
		.result = thr,
		.errorcode = OK,
	};

	switch (mode) {
	case const_rta_mode:
		result.result.rta = (unsigned int)(strtod(threshold_string, &resultChecker) * 1000);
		break;
	case const_packet_loss_mode:
		result.result.pl = (unsigned char)strtoul(threshold_string, &resultChecker, 0);
		break;
	case const_jitter_mode:
		result.result.jitter = strtod(threshold_string, &resultChecker);
		break;
	case const_mos_mode:
		result.result.mos = strtod(threshold_string, &resultChecker);
		break;
	case const_score_mode:
		result.result.score = strtod(threshold_string, &resultChecker);
		break;
	}

	if (resultChecker == threshold_string) {
		// Failed to parse
		result.errorcode = ERROR;
		return result;
	}

	if (resultChecker != (threshold_string + length)) {
		// Trailing symbols
		result.errorcode = ERROR;
	}

	return result;
}

unsigned short icmp_checksum(uint16_t *packet, size_t packet_size) {
	long sum = 0;

	/* sizeof(uint16_t) == 2 */
	while (packet_size >= 2) {
		sum += *(packet++);
		packet_size -= 2;
	}

	/* mop up the occasional odd byte */
	if (packet_size == 1) {
		sum += *((uint8_t *)packet - 1);
	}

	sum = (sum >> 16) + (sum & 0xffff); /* add hi 16 to low 16 */
	sum += (sum >> 16);                 /* add carry */
	unsigned short cksum;
	cksum = (unsigned short)~sum; /* ones-complement, trunc to 16 bits */

	return cksum;
}

void print_help(void) {
	/*print_revision (progname);*/ /* FIXME: Why? */
	printf("Copyright (c) 2005 Andreas Ericsson <ae@op5.se>\n");

	printf(COPYRIGHT, copyright, email);

	printf("\n\n");

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" %s\n", "-H");
	printf("    %s\n", _("specify a target"));
	printf(" %s\n", "[-4|-6]");
	printf("    %s\n", _("Use IPv4 (default) or IPv6 to communicate with the targets"));
	printf(" %s\n", "-w");
	printf("    %s", _("warning threshold (currently "));
	printf("%0.3fms,%u%%)\n", (float)DEFAULT_WARN_RTA / 1000, DEFAULT_WARN_PL);
	printf(" %s\n", "-c");
	printf("    %s", _("critical threshold (currently "));
	printf("%0.3fms,%u%%)\n", (float)DEFAULT_CRIT_RTA / 1000, DEFAULT_CRIT_PL);

	printf(" %s\n", "-R");
	printf("    %s\n",
		   _("RTA, round trip average,  mode  warning,critical, ex. 100ms,200ms unit in ms"));
	printf(" %s\n", "-P");
	printf("    %s\n", _("packet loss mode, ex. 40%,50% , unit in %"));
	printf(" %s\n", "-J");
	printf("    %s\n", _("jitter mode  warning,critical, ex. 40.000ms,50.000ms , unit in ms "));
	printf(" %s\n", "-M");
	printf("    %s\n", _("MOS mode, between 0 and 4.4  warning,critical, ex. 3.5,3.0"));
	printf(" %s\n", "-S");
	printf("    %s\n", _("score  mode, max value 100  warning,critical, ex. 80,70 "));
	printf(" %s\n", "-O");
	printf("    %s\n", _("detect out of order ICMP packts "));
	printf(" %s\n", "-H");
	printf("    %s\n", _("specify a target"));
	printf(" %s\n", "-s");
	printf("    %s\n", _("specify a source IP address or device name"));
	printf(" %s\n", "-n");
	printf("    %s", _("number of packets to send (currently "));
	printf("%u)\n", DEFAULT_NUMBER_OF_PACKETS);
	printf(" %s\n", "-p");
	printf("    %s", _("number of packets to send (currently "));
	printf("%u)\n", DEFAULT_NUMBER_OF_PACKETS);
	printf(" %s\n", "-i");
	printf("    %s", _("max packet interval (currently "));
	printf("%0.3fms)\n", (float)DEFAULT_PKT_INTERVAL / 1000);
	printf(" %s\n", "-I");
	printf("    %s", _("max target interval (currently "));
	printf("%0.3fms)\n", (float)DEFAULT_TARGET_INTERVAL / 1000);
	printf(" %s\n", "-m");
	printf("    %s", _("number of alive hosts required for success"));
	printf("\n");
	printf(" %s\n", "-l");
	printf("    %s", _("TTL on outgoing packets (currently "));
	printf("%u)\n", DEFAULT_TTL);
	printf(" %s\n", "-t");
	printf("    %s", _("timeout value (seconds, currently  "));
	printf("%u)\n", DEFAULT_TIMEOUT);
	printf(" %s\n", "-b");
	printf("    %s\n", _("Number of icmp data bytes to send"));
	printf("    %s %lu + %d)\n", _("Packet size will be data bytes + icmp header (currently"),
		   DEFAULT_PING_DATA_SIZE, ICMP_MINLEN);
	printf(" %s\n", "-v");
	printf("    %s\n", _("verbose"));
	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("If none of R,P,J,M,S or O is specified, default behavior is -R -P"));
	printf(" %s\n", _("The -H switch is optional. Naming a host (or several) to check is not."));
	printf("\n");
	printf(" %s\n", _("Threshold format for -w and -c is 200.25,60% for 200.25 msec RTA and 60%"));
	printf(" %s\n", _("packet loss.  The default values should work well for most users."));
	printf(" %s\n",
		   _("You can specify different RTA factors using the standardized abbreviations"));
	printf(" %s\n",
		   _("us (microseconds), ms (milliseconds, default) or just plain s for seconds."));
	/* -d not yet implemented */
	/*  printf ("%s\n", _("Threshold format for -d is warn,crit.  12,14 means WARNING if >= 12
	   hops")); printf ("%s\n", _("are spent and CRITICAL if >= 14 hops are spent.")); printf
	   ("%s\n\n", _("NOTE: Some systems decrease TTL when forming ICMP_ECHOREPLY, others do
	   not."));*/
	printf("\n");
	printf(" %s\n", _("The -v switch can be specified several times for increased verbosity."));
	/*  printf ("%s\n", _("Long options are currently unsupported."));
			printf ("%s\n", _("Options marked with * require an argument"));
			*/

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s [options] [-H] host1 host2 hostN\n", progname);
}
