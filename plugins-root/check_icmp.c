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
#include "output.h"
#include "perfdata.h"

#if HAVE_SYS_SOCKIO_H
#	include <sys/sockio.h>
#endif

#include <sys/time.h>
#if defined(SIOCGIFADDR)
#	include <sys/ioctl.h>
#endif /* SIOCGIFADDR */
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
#include <sys/socket.h>
#include <assert.h>
#include <sys/select.h>

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
typedef struct {
	int error_code;
	time_t time_range;
} get_timevar_wrapper;
static get_timevar_wrapper get_timevar(const char *str);
static time_t get_timevaldiff(struct timeval earlier, struct timeval later);
static time_t get_timevaldiff_to_now(struct timeval earlier);

static in_addr_t get_ip_address(const char *ifname, const int icmp_sock);
static void set_source_ip(char *arg, int icmp_sock, sa_family_t addr_family);

/* Receiving data */
static int wait_for_reply(check_icmp_socket_set sockset, time_t time_interval,
						  unsigned short icmp_pkt_size, time_t *target_interval, uint16_t sender_id,
						  ping_target **table, unsigned short packets,
						  unsigned short number_of_targets, check_icmp_state *program_state);

typedef struct {
	sa_family_t recv_proto;
	ssize_t received;
} recvfrom_wto_wrapper;
static recvfrom_wto_wrapper recvfrom_wto(check_icmp_socket_set sockset, void *buf, unsigned int len,
										 struct sockaddr *saddr, time_t *timeout,
										 struct timeval *received_timestamp);
static int handle_random_icmp(unsigned char *packet, struct sockaddr_storage *addr,
							  time_t *target_interval, uint16_t sender_id, ping_target **table,
							  unsigned short packets, unsigned short number_of_targets,
							  check_icmp_state *program_state);

/* Sending data */
static int send_icmp_ping(check_icmp_socket_set sockset, ping_target *host,
						  unsigned short icmp_pkt_size, uint16_t sender_id,
						  check_icmp_state *program_state);

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
static void run_checks(unsigned short icmp_pkt_size, time_t *target_interval, uint16_t sender_id,
					   check_icmp_execution_mode mode, time_t max_completion_time,
					   struct timeval prog_start, ping_target **table, unsigned short packets,
					   check_icmp_socket_set sockset, unsigned short number_of_targets,
					   check_icmp_state *program_state);
mp_subcheck evaluate_target(ping_target target, check_icmp_mode_switches modes,
							check_icmp_threshold warn, check_icmp_threshold crit);

typedef struct {
	int targets_ok;
	int targets_warn;
	mp_subcheck sc_host;
} evaluate_host_wrapper;
evaluate_host_wrapper evaluate_host(check_icmp_target_container host,
									check_icmp_mode_switches modes, check_icmp_threshold warn,
									check_icmp_threshold crit);

/* Target acquisition */
typedef struct {
	int error_code;
	check_icmp_target_container host;
	bool has_v4;
	bool has_v6;
} add_host_wrapper;
static add_host_wrapper add_host(char *arg, check_icmp_execution_mode mode,
								 sa_family_t enforced_proto);

typedef struct {
	int error_code;
	ping_target *targets;
	unsigned int number_of_targets;
	bool has_v4;
	bool has_v6;
} add_target_wrapper;
static add_target_wrapper add_target(char *arg, check_icmp_execution_mode mode,
									 sa_family_t enforced_proto);

typedef struct {
	int error_code;
	ping_target *target;
} add_target_ip_wrapper;
static add_target_ip_wrapper add_target_ip(struct sockaddr_storage address);

static void parse_address(const struct sockaddr_storage *addr, char *dst, socklen_t size);

static unsigned short icmp_checksum(uint16_t *packet, size_t packet_size);

/* End of run function */
static void finish(int sign, check_icmp_mode_switches modes, int min_hosts_alive,
				   check_icmp_threshold warn, check_icmp_threshold crit,
				   unsigned short number_of_targets, check_icmp_state *program_state,
				   check_icmp_target_container host_list[], unsigned short number_of_hosts,
				   mp_check overall[static 1]);

/* Error exit */
static void crash(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

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
check_icmp_config_wrapper process_arguments(int argc, char **argv) {
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
		result.config.number_of_packets = 5;
		result.config.crit.rta = result.config.warn.rta = 1000000;
		result.config.crit.pl = result.config.warn.pl = 100;
	} else if (!strcmp(progname, "check_rta_multi")) {
		result.config.mode = MODE_ALL;
		result.config.target_interval = 0;
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

	sa_family_t enforced_ai_family = AF_UNSPEC;

	enum {
		output_format_index = CHAR_MAX + 1,
	};

	struct option longopts[] = {
		{"version", no_argument, 0, 'V'},
		{"help", no_argument, 0, 'h'},
		{"verbose", no_argument, 0, 'v'},
		{"Host", required_argument, 0, 'H'},
		{"ipv4-only", no_argument, 0, '4'},
		{"ipv6-only", no_argument, 0, '6'},
		{"warning", required_argument, 0, 'w'},
		{"critical", required_argument, 0, 'c'},
		{"rta-mode-thresholds", required_argument, 0, 'R'},
		{"packet-loss-mode-thresholds", required_argument, 0, 'P'},
		{"jitter-mode-thresholds", required_argument, 0, 'J'},
		{"mos-mode-thresholds", required_argument, 0, 'M'},
		{"score-mode-thresholds", required_argument, 0, 'S'},
		{"out-of-order-packets", no_argument, 0, 'O'},
		{"number-of-packets", required_argument, 0, 'n'},
		{"number-of-packets", required_argument, 0, 'p'},
		{"packet-interval", required_argument, 0, 'i'},
		{"target-interval", required_argument, 0, 'I'},
		{"minimal-host-alive", required_argument, 0, 'm'},
		{"outgoing-ttl", required_argument, 0, 'l'},
		{"size", required_argument, 0, 'b'},
		{"output-format", required_argument, 0, output_format_index},
		{},
	};

	// Parse protocol arguments first
	// and count hosts here
	char *opts_str = "vhVw:c:n:p:t:H:s:i:b:I:l:m:P:R:J:S:M:O64";
	for (int i = 1; i < argc; i++) {
		long int arg;
		while ((arg = getopt_long(argc, argv, opts_str, longopts, NULL)) != EOF) {
			switch (arg) {

			case '4':
				if (enforced_ai_family != AF_UNSPEC) {
					crash("Multiple protocol versions not supported");
				}
				enforced_ai_family = AF_INET;
				break;
			case '6':
				if (enforced_ai_family != AF_UNSPEC) {
					crash("Multiple protocol versions not supported");
				}
				enforced_ai_family = AF_INET6;
				break;
			case 'H': {
				result.config.number_of_hosts++;
				break;
			}
			case 'h': /* help */
				// Trigger help here to avoid adding hosts before that (and doing DNS queries)
				print_help();
				exit(STATE_UNKNOWN);
				break;
			case 'v':
				debug++;
				break;
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

	int host_counter = 0;
	/* parse the arguments */
	for (int i = 1; i < argc; i++) {
		long int arg;
		while ((arg = getopt_long(argc, argv, opts_str, longopts, NULL)) != EOF) {
			switch (arg) {
			case 'b': {
				long size = strtol(optarg, NULL, 0);
				if ((unsigned long)size >= (sizeof(struct icmp) + sizeof(struct icmp_ping_data)) &&
					size < MAX_PING_DATA) {
					result.config.icmp_data_size = (unsigned short)size;
				} else {
					usage_va("ICMP data length must be between: %lu and %lu",
							 sizeof(struct icmp) + sizeof(struct icmp_ping_data),
							 MAX_PING_DATA - 1);
				}
			} break;
			case 'i': {
				// packet_interval was unused and is now removed
			} break;
			case 'I': {
				get_timevar_wrapper parsed_time = get_timevar(optarg);

				if (parsed_time.error_code == OK) {
					result.config.target_interval = parsed_time.time_range;
				} else {
					crash("failed to parse target interval");
				}
			} break;
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
				// WARNING Deprecated since execution time is determined by the other factors
				break;
			case 'H': {
				add_host_wrapper host_add_result =
					add_host(optarg, result.config.mode, enforced_ai_family);
				if (host_add_result.error_code == OK) {
					result.config.hosts[host_counter] = host_add_result.host;
					host_counter++;

					if (result.config.targets != NULL) {
						result.config.number_of_targets += ping_target_list_append(
							result.config.targets, host_add_result.host.target_list);
					} else {
						result.config.targets = host_add_result.host.target_list;
						result.config.number_of_targets += host_add_result.host.number_of_targets;
					}

					if (host_add_result.has_v4) {
						result.config.need_v4 = true;
					}
					if (host_add_result.has_v6) {
						result.config.need_v6 = true;
					}
				} else {
					crash("Failed to add host, unable to parse it correctly");
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
			case 'R': /* RTA mode */ {
				get_threshold2_wrapper rta_th = get_threshold2(
					optarg, strlen(optarg), result.config.warn, result.config.crit, const_rta_mode);

				if (rta_th.errorcode != OK) {
					crash("Failed to parse RTA threshold");
				}

				result.config.warn = rta_th.warn;
				result.config.crit = rta_th.crit;
				result.config.modes.rta_mode = true;
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
				result.config.modes.pl_mode = true;
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
				result.config.modes.jitter_mode = true;
			} break;
			case 'M': /* MOS mode */ {
				get_threshold2_wrapper mos_th = get_threshold2(
					optarg, strlen(optarg), result.config.warn, result.config.crit, const_mos_mode);
				if (mos_th.errorcode != OK) {
					crash("Failed to parse MOS threshold");
				}

				result.config.warn = mos_th.warn;
				result.config.crit = mos_th.crit;
				result.config.modes.mos_mode = true;
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
				result.config.modes.score_mode = true;
			} break;
			case 'O': /* out of order mode */
				result.config.modes.order_mode = true;
				break;
			case output_format_index: {
				parsed_output_format parser = mp_parse_output_format(optarg);
				if (!parser.parsing_success) {
					// TODO List all available formats here, maybe add anothoer usage function
					printf("Invalid output format: %s\n", optarg);
					exit(STATE_UNKNOWN);
				}

				result.config.output_format_is_set = true;
				result.config.output_format = parser.output_format;
				break;
			}
			}
		}
	}

	argv = &argv[optind];
	while (*argv) {
		add_target(*argv, result.config.mode, enforced_ai_family);
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
							  time_t *target_interval, const uint16_t sender_id,
							  ping_target **table, unsigned short packets,
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
		printf("Received \"%s\" from %s for ICMP ECHO sent.\n",
			   get_icmp_error_msg(icmp_packet.icmp_type, icmp_packet.icmp_code), address);
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
		*target_interval = (unsigned int)((double)*target_interval * TARGET_BACKOFF_FACTOR);
	} else {
		program_state->targets_down++;
		host->flags |= FLAG_LOST_CAUSE;
	}
	host->icmp_type = icmp_packet.icmp_type;
	host->icmp_code = icmp_packet.icmp_code;
	host->error_addr = *addr;

	return 0;
}

void parse_address(const struct sockaddr_storage *addr, char *dst, socklen_t size) {
	switch (addr->ss_family) {
	case AF_INET:
		inet_ntop(AF_INET, &((struct sockaddr_in *)addr)->sin_addr, dst, size);
		break;
	case AF_INET6:
		inet_ntop(AF_INET6, &((struct sockaddr_in6 *)addr)->sin6_addr, dst, size);
		break;
	default:
		assert(false);
	}
}

int main(int argc, char **argv) {
	setlocale(LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);

	/* POSIXLY_CORRECT might break things, so unset it (the portable way) */
	environ = NULL;

	/* determine program- and service-name quickly */
	progname = strrchr(argv[0], '/');
	if (progname != NULL) {
		progname++;
	} else {
		progname = argv[0];
	}

	/* Parse extra opts if any */
	argv = np_extra_opts(&argc, argv, progname);

	check_icmp_config_wrapper tmp_config = process_arguments(argc, argv);

	if (tmp_config.errorcode != OK) {
		crash("failed to parse config");
	}

	const check_icmp_config config = tmp_config.config;

	if (config.output_format_is_set) {
		mp_set_format(config.output_format);
	}

	check_icmp_socket_set sockset = {
		.socket4 = -1,
		.socket6 = -1,
	};

	if (config.need_v4) {
		sockset.socket4 = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
		if (sockset.socket4 == -1) {
			crash("Failed to obtain ICMP v4 socket");
		}

		if (config.source_ip) {

			struct in_addr tmp = {};
			int error_code = inet_pton(AF_INET, config.source_ip, &tmp);
			if (error_code == 1) {
				set_source_ip(config.source_ip, sockset.socket4, AF_INET);
			} else {
				// just try this mindlessly if it's not a v4 address
				set_source_ip(config.source_ip, sockset.socket6, AF_INET6);
			}
		}

#ifdef SO_TIMESTAMP
		if (sockset.socket4 != -1) {
			int on = 1;
			if (setsockopt(sockset.socket4, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on))) {
				if (debug) {
					printf("Warning: no SO_TIMESTAMP support\n");
				}
			}
		}
		if (sockset.socket6 != -1) {
			int on = 1;
			if (setsockopt(sockset.socket6, SOL_SOCKET, SO_TIMESTAMP, &on, sizeof(on))) {
				if (debug) {
					printf("Warning: no SO_TIMESTAMP support\n");
				}
			}
		}
#endif // SO_TIMESTAMP
	}

	if (config.need_v6) {
		sockset.socket6 = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
		if (sockset.socket6 == -1) {
			crash("Failed to obtain ICMP v6 socket");
		}
	}

	/* now drop privileges (no effect if not setsuid or geteuid() == 0) */
	if (setuid(getuid()) == -1) {
		printf("ERROR: Failed to drop privileges\n");
		return 1;
	}

	if (sockset.socket4) {
		int result = setsockopt(sockset.socket4, SOL_IP, IP_TTL, &config.ttl, sizeof(config.ttl));
		if (debug) {
			if (result == -1) {
				printf("setsockopt failed\n");
			} else {
				printf("ttl set to %lu\n", config.ttl);
			}
		}
	}

	if (sockset.socket6) {
		int result = setsockopt(sockset.socket6, SOL_IP, IP_TTL, &config.ttl, sizeof(config.ttl));
		if (debug) {
			if (result == -1) {
				printf("setsockopt failed\n");
			} else {
				printf("ttl set to %lu\n", config.ttl);
			}
		}
	}

	/* make sure we don't wait any longer than necessary */
	struct timeval prog_start;
	gettimeofday(&prog_start, NULL);

	time_t max_completion_time =
		(config.target_interval * config.number_of_targets) +
		(config.crit.rta * config.number_of_targets * config.number_of_packets) + config.crit.rta;

	if (debug) {
		printf("packets: %u, targets: %u\n"
			   "target_interval: %0.3f\n"
			   "crit.rta: %0.3f\n"
			   "max_completion_time: %0.3f\n",
			   config.number_of_packets, config.number_of_targets,
			   (float)config.target_interval / 1000, (float)config.crit.rta / 1000,
			   (float)max_completion_time / 1000);
	}

	if (debug) {
		if (max_completion_time > (timeout * 1000000)) {
			printf("max_completion_time: %ld  timeout: %u\n", max_completion_time, timeout);
			printf("Timeout must be at least %ld\n", (max_completion_time / 1000000) + 1);
		}
	}

	if (debug) {
		printf("crit = {%ld, %u%%}, warn = {%ld, %u%%}\n", config.crit.rta, config.crit.pl,
			   config.warn.rta, config.warn.pl);
		printf("target_interval: %ld\n", config.target_interval);
		printf("icmp_pkt_size: %u  timeout: %u\n", config.icmp_data_size + ICMP_MINLEN, timeout);
	}

	if (config.min_hosts_alive < -1) {
		errno = 0;
		crash("minimum alive hosts is negative (%i)", config.min_hosts_alive);
	}

	// Build an index table of all targets
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

	time_t target_interval = config.target_interval;

	check_icmp_state program_state = check_icmp_state_init();

	run_checks(config.icmp_data_size, &target_interval, config.sender_id, config.mode,
			   max_completion_time, prog_start, table, config.number_of_packets, sockset,
			   config.number_of_targets, &program_state);

	errno = 0;

	mp_check overall = mp_check_init();
	finish(0, config.modes, config.min_hosts_alive, config.warn, config.crit,
		   config.number_of_targets, &program_state, config.hosts, config.number_of_hosts,
		   &overall);

	if (sockset.socket4) {
		close(sockset.socket4);
	}
	if (sockset.socket6) {
		close(sockset.socket6);
	}

	mp_exit(overall);
}

static void run_checks(unsigned short icmp_pkt_size, time_t *target_interval,
					   const uint16_t sender_id, const check_icmp_execution_mode mode,
					   const time_t max_completion_time, const struct timeval prog_start,
					   ping_target **table, const unsigned short packets,
					   const check_icmp_socket_set sockset, const unsigned short number_of_targets,
					   check_icmp_state *program_state) {
	/* this loop might actually violate the pkt_interval or target_interval
	 * settings, but only if there aren't any packets on the wire which
	 * indicates that the target can handle an increased packet rate */
	for (unsigned int packet_index = 0; packet_index < packets; packet_index++) {
		for (unsigned int target_index = 0; target_index < number_of_targets; target_index++) {
			/* don't send useless packets */
			if (!targets_alive(number_of_targets, program_state->targets_down)) {
				return;
			}
			if (table[target_index]->flags & FLAG_LOST_CAUSE) {
				if (debug) {

					char address[INET6_ADDRSTRLEN];
					parse_address(&table[target_index]->address, address, sizeof(address));
					printf("%s is a lost cause. not sending any more\n", address);
				}
				continue;
			}

			/* we're still in the game, so send next packet */
			(void)send_icmp_ping(sockset, table[target_index], icmp_pkt_size, sender_id,
								 program_state);

			/* wrap up if all targets are declared dead */
			if (targets_alive(number_of_targets, program_state->targets_down) ||
				get_timevaldiff(prog_start, prog_start) < max_completion_time ||
				!(mode == MODE_HOSTCHECK && program_state->targets_down)) {
				wait_for_reply(sockset, *target_interval, icmp_pkt_size, target_interval, sender_id,
							   table, packets, number_of_targets, program_state);
			}
		}
		if (targets_alive(number_of_targets, program_state->targets_down) ||
			get_timevaldiff_to_now(prog_start) < max_completion_time ||
			!(mode == MODE_HOSTCHECK && program_state->targets_down)) {
			wait_for_reply(sockset, number_of_targets, icmp_pkt_size, target_interval, sender_id,
						   table, packets, number_of_targets, program_state);
		}
	}

	if (icmp_pkts_en_route(program_state->icmp_sent, program_state->icmp_recv,
						   program_state->icmp_lost) &&
		targets_alive(number_of_targets, program_state->targets_down)) {
		time_t time_passed = get_timevaldiff_to_now(prog_start);
		time_t final_wait = max_completion_time - time_passed;

		if (debug) {
			printf("time_passed: %ld  final_wait: %ld  max_completion_time: %ld\n", time_passed,
				   final_wait, max_completion_time);
		}
		if (time_passed > max_completion_time) {
			if (debug) {
				printf("Time passed. Finishing up\n");
			}
			return;
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
			wait_for_reply(sockset, final_wait, icmp_pkt_size, target_interval, sender_id, table,
						   packets, number_of_targets, program_state);
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
static int wait_for_reply(check_icmp_socket_set sockset, const time_t time_interval,
						  unsigned short icmp_pkt_size, time_t *target_interval, uint16_t sender_id,
						  ping_target **table, const unsigned short packets,
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
		recvfrom_wto_wrapper recv_foo =
			recvfrom_wto(sockset, buf, sizeof(buf), (struct sockaddr *)&resp_addr,
						 &loop_time_interval, &packet_received_timestamp);
		if (!recv_foo.received) {
			if (debug > 1) {
				printf("recvfrom_wto() timed out during a %ld usecs wait\n", per_pkt_wait);
			}
			continue; /* timeout for this one, so keep trying */
		}

		if (recv_foo.received < 0) {
			if (debug) {
				printf("recvfrom_wto() returned errors\n");
			}
			free(packet.buf);
			return (int)recv_foo.received;
		}

		if (recv_foo.recv_proto != AF_INET6) {
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

		int hlen = (recv_foo.recv_proto == AF_INET6) ? 0 : ip_header->ip.ip_hl << 2;

		if (recv_foo.received < (hlen + ICMP_MINLEN)) {
			char address[INET6_ADDRSTRLEN];
			parse_address(&resp_addr, address, sizeof(address));
			crash("received packet too short for ICMP (%ld bytes, expected %d) from %s\n",
				  recv_foo.received, hlen + icmp_pkt_size, address);
		}
		/* check the response */
		memcpy(packet.buf, buf + hlen, icmp_pkt_size);

		if ((recv_foo.recv_proto == AF_INET &&
			 (ntohs(packet.icp->icmp_id) != sender_id || packet.icp->icmp_type != ICMP_ECHOREPLY ||
			  ntohs(packet.icp->icmp_seq) >= number_of_targets * packets)) ||
			(recv_foo.recv_proto == AF_INET6 &&
			 (ntohs(packet.icp6->icmp6_id) != sender_id ||
			  packet.icp6->icmp6_type != ICMP6_ECHO_REPLY ||
			  ntohs(packet.icp6->icmp6_seq) >= number_of_targets * packets))) {
			if (debug > 2) {
				printf("not a proper ICMP_ECHOREPLY\n");
			}

			handle_random_icmp(buf + hlen, &resp_addr, target_interval, sender_id, table, packets,
							   number_of_targets, program_state);

			continue;
		}

		/* this is indeed a valid response */
		ping_target *target;
		struct icmp_ping_data data;
		if (address_family == AF_INET) {
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
				target->found_out_of_order_packets = true;
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

			switch (recv_foo.recv_proto) {
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
static int send_icmp_ping(const check_icmp_socket_set sockset, ping_target *host,
						  const unsigned short icmp_pkt_size, const uint16_t sender_id,
						  check_icmp_state *program_state) {
	void *buf = calloc(1, icmp_pkt_size);
	if (!buf) {
		crash("send_icmp_ping(): failed to malloc %d bytes for send buffer", icmp_pkt_size);
		return -1; /* might be reached if we're in debug mode */
	}

	struct timeval current_time;
	if ((gettimeofday(&current_time, NULL)) == -1) {
		free(buf);
		return -1;
	}

	struct icmp_ping_data data;
	data.ping_id = 10; /* host->icmp.icmp_sent; */
	memcpy(&data.stime, &current_time, sizeof(current_time));

	socklen_t addrlen = 0;

	if (host->address.ss_family == AF_INET) {
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
			char address[INET6_ADDRSTRLEN];
			parse_address((&host->address), address, sizeof(address));

			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to host %s\n",
				   sizeof(data), ntohs(icp->icmp_id), ntohs(icp->icmp_seq), icp->icmp_cksum,
				   address);
		}
	} else if (host->address.ss_family == AF_INET6) {
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
			char address[INET6_ADDRSTRLEN];
			parse_address((&host->address), address, sizeof(address));

			printf("Sending ICMP echo-request of len %lu, id %u, seq %u, cksum 0x%X to target %s\n",
				   sizeof(data), ntohs(icp6->icmp6_id), ntohs(icp6->icmp6_seq), icp6->icmp6_cksum,
				   address);
		}
	} else {
		// unknown address family
		crash("unknown address family in %s", __func__);
	}

	struct iovec iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = icmp_pkt_size;

	struct msghdr hdr;
	memset(&hdr, 0, sizeof(hdr));
	hdr.msg_name = (struct sockaddr *)&host->address;
	hdr.msg_namelen = addrlen;
	hdr.msg_iov = &iov;
	hdr.msg_iovlen = 1;

	errno = 0;

	long int len;
	/* MSG_CONFIRM is a linux thing and only available on linux kernels >= 2.3.15, see send(2) */
	if (host->address.ss_family == AF_INET) {
#ifdef MSG_CONFIRM
		len = sendmsg(sockset.socket4, &hdr, MSG_CONFIRM);
#else
		len = sendmsg(sockset.socket4, &hdr, 0);
#endif
	} else if (host->address.ss_family == AF_INET6) {
#ifdef MSG_CONFIRM
		len = sendmsg(sockset.socket6, &hdr, MSG_CONFIRM);
#else
		len = sendmsg(sockset.socket6, &hdr, 0);
#endif
	} else {
		assert(false);
	}

	free(buf);

	if (len < 0 || (unsigned int)len != icmp_pkt_size) {
		if (debug) {
			char address[INET6_ADDRSTRLEN];
			parse_address((&host->address), address, sizeof(address));
			printf("Failed to send ping to %s: %s\n", address, strerror(errno));
		}
		errno = 0;
		return -1;
	}

	program_state->icmp_sent++;
	host->icmp_sent++;

	return 0;
}

static recvfrom_wto_wrapper recvfrom_wto(const check_icmp_socket_set sockset, void *buf,
										 const unsigned int len, struct sockaddr *saddr,
										 time_t *timeout, struct timeval *received_timestamp) {
#ifdef HAVE_MSGHDR_MSG_CONTROL
	char ans_data[4096];
#endif // HAVE_MSGHDR_MSG_CONTROL
#ifdef SO_TIMESTAMP
	struct cmsghdr *chdr;
#endif

	recvfrom_wto_wrapper result = {
		.received = 0,
		.recv_proto = AF_UNSPEC,
	};

	if (!*timeout) {
		if (debug) {
			printf("*timeout is not\n");
		}
		return result;
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

	if (sockset.socket4 != -1) {
		FD_SET(sockset.socket4, &read_fds);
	}
	if (sockset.socket6 != -1) {
		FD_SET(sockset.socket6, &read_fds);
	}

	int nfds = (sockset.socket4 > sockset.socket6 ? sockset.socket4 : sockset.socket6) + 1;

	struct timeval then;
	gettimeofday(&then, NULL);

	errno = 0;
	int select_return = select(nfds, &read_fds, &dummy_write_fds, NULL, &real_timeout);
	if (select_return < 0) {
		crash("select() in recvfrom_wto");
	}

	struct timeval now;
	gettimeofday(&now, NULL);
	*timeout = get_timevaldiff(then, now);

	if (!select_return) {
		return result; /* timeout */
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

	ssize_t ret;

	// Test explicitly whether sockets are in use
	// this is necessary at least on OpenBSD where FD_ISSET will segfault otherwise
	if ((sockset.socket4 != -1) && FD_ISSET(sockset.socket4, &read_fds)) {
		ret = recvmsg(sockset.socket4, &hdr, 0);
		result.recv_proto = AF_INET;
	} else if ((sockset.socket6 != -1) && FD_ISSET(sockset.socket6, &read_fds)) {
		ret = recvmsg(sockset.socket6, &hdr, 0);
		result.recv_proto = AF_INET6;
	} else {
		assert(false);
	}

	result.received = ret;

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

	return (result);
}

static void finish(int sig, check_icmp_mode_switches modes, int min_hosts_alive,
				   check_icmp_threshold warn, check_icmp_threshold crit,
				   const unsigned short number_of_targets, check_icmp_state *program_state,
				   check_icmp_target_container host_list[], unsigned short number_of_hosts,
				   mp_check overall[static 1]) {
	// Deactivate alarm
	alarm(0);

	if (debug > 1) {
		printf("finish(%d) called\n", sig);
	}

	if (debug) {
		printf("icmp_sent: %u  icmp_recv: %u  icmp_lost: %u\n", program_state->icmp_sent,
			   program_state->icmp_recv, program_state->icmp_lost);
		printf("targets: %u  targets_alive: %u\n", number_of_targets,
			   targets_alive(number_of_targets, program_state->targets_down));
	}

	// loop over targets to evaluate each one
	int targets_ok = 0;
	int targets_warn = 0;
	for (unsigned short i = 0; i < number_of_hosts; i++) {
		evaluate_host_wrapper host_check = evaluate_host(host_list[i], modes, warn, crit);

		targets_ok += host_check.targets_ok;
		targets_warn += host_check.targets_warn;

		mp_add_subcheck_to_check(overall, host_check.sc_host);
	}

	if (min_hosts_alive > -1) {
		mp_subcheck sc_min_targets_alive = mp_subcheck_init();
		sc_min_targets_alive = mp_set_subcheck_default_state(sc_min_targets_alive, STATE_OK);

		if (targets_ok >= min_hosts_alive) {
			sc_min_targets_alive = mp_set_subcheck_state(sc_min_targets_alive, STATE_OK);
			xasprintf(&sc_min_targets_alive.output, "%u targets OK of a minimum of %u", targets_ok,
					  min_hosts_alive);

			// Overwrite main state here
			overall->evaluation_function = &mp_eval_ok;
		} else if ((targets_ok + targets_warn) >= min_hosts_alive) {
			sc_min_targets_alive = mp_set_subcheck_state(sc_min_targets_alive, STATE_WARNING);
			xasprintf(&sc_min_targets_alive.output, "%u targets OK or Warning of a minimum of %u",
					  targets_ok + targets_warn, min_hosts_alive);
			overall->evaluation_function = &mp_eval_warning;
		} else {
			sc_min_targets_alive = mp_set_subcheck_state(sc_min_targets_alive, STATE_CRITICAL);
			xasprintf(&sc_min_targets_alive.output, "%u targets OK or Warning of a minimum of %u",
					  targets_ok + targets_warn, min_hosts_alive);
			overall->evaluation_function = &mp_eval_critical;
		}

		mp_add_subcheck_to_check(overall, sc_min_targets_alive);
	}

	/* finish with an empty line */
	if (debug) {
		printf(
			"targets: %u, targets_alive: %u, hosts_ok: %u, hosts_warn: %u, min_hosts_alive: %i\n",
			number_of_targets, targets_alive(number_of_targets, program_state->targets_down),
			targets_ok, targets_warn, min_hosts_alive);
	}
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

static add_target_ip_wrapper add_target_ip(struct sockaddr_storage address) {
	assert((address.ss_family == AF_INET) || (address.ss_family == AF_INET6));

	if (debug) {
		char straddr[INET6_ADDRSTRLEN];
		parse_address((&address), straddr, sizeof(straddr));
		printf("add_target_ip called with: %s\n", straddr);
	}
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	if (address.ss_family == AF_INET) {
		sin = (struct sockaddr_in *)&address;
	} else if (address.ss_family == AF_INET6) {
		sin6 = (struct sockaddr_in6 *)&address;
	} else {
		assert(false);
	}

	add_target_ip_wrapper result = {
		.error_code = OK,
		.target = NULL,
	};

	/* disregard obviously stupid addresses
	 * (I didn't find an ipv6 equivalent to INADDR_NONE) */
	if (((address.ss_family == AF_INET &&
		  (sin->sin_addr.s_addr == INADDR_NONE || sin->sin_addr.s_addr == INADDR_ANY))) ||
		(address.ss_family == AF_INET6 && (sin6->sin6_addr.s6_addr == in6addr_any.s6_addr))) {
		result.error_code = ERROR;
		return result;
	}

	// get string representation of address
	char straddr[INET6_ADDRSTRLEN];
	parse_address((&address), straddr, sizeof(straddr));

	/* add the fresh ip */
	ping_target *target = (ping_target *)calloc(1, sizeof(ping_target));
	if (!target) {
		crash("add_target_ip(%s): malloc(%lu) failed", straddr, sizeof(ping_target));
	}

	ping_target_create_wrapper target_wrapper = ping_target_create(address);

	if (target_wrapper.errorcode == OK) {
		*target = target_wrapper.host;
		result.target = target;
	} else {
		result.error_code = target_wrapper.errorcode;
	}

	return result;
}

/* wrapper for add_target_ip */
static add_target_wrapper add_target(char *arg, const check_icmp_execution_mode mode,
									 sa_family_t enforced_proto) {
	if (debug > 0) {
		printf("add_target called with argument %s\n", arg);
	}

	struct sockaddr_storage address_storage = {};
	struct sockaddr_in *sin = NULL;
	struct sockaddr_in6 *sin6 = NULL;
	int error_code = -1;

	switch (enforced_proto) {
	case AF_UNSPEC:
		/*
		 * no enforced protocol family
		 * try to parse the address with each one
		 */
		sin = (struct sockaddr_in *)&address_storage;
		error_code = inet_pton(AF_INET, arg, &sin->sin_addr);
		address_storage.ss_family = AF_INET;

		if (error_code != 1) {
			sin6 = (struct sockaddr_in6 *)&address_storage;
			error_code = inet_pton(AF_INET6, arg, &sin6->sin6_addr);
			address_storage.ss_family = AF_INET6;
		}
		break;
	case AF_INET:
		sin = (struct sockaddr_in *)&address_storage;
		error_code = inet_pton(AF_INET, arg, &sin->sin_addr);
		address_storage.ss_family = AF_INET;
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)&address_storage;
		error_code = inet_pton(AF_INET, arg, &sin6->sin6_addr);
		address_storage.ss_family = AF_INET6;
		break;
	default:
		crash("Address family not supported");
	}

	add_target_wrapper result = {
		.error_code = OK,
		.targets = NULL,
		.has_v4 = false,
		.has_v6 = false,
	};

	// if error_code == 1 the address was a valid address parsed above
	if (error_code == 1) {
		/* don't add all ip's if we were given a specific one */
		add_target_ip_wrapper targeted = add_target_ip(address_storage);

		if (targeted.error_code != OK) {
			result.error_code = ERROR;
			return result;
		}

		if (targeted.target->address.ss_family == AF_INET) {
			result.has_v4 = true;
		} else if (targeted.target->address.ss_family == AF_INET6) {
			result.has_v6 = true;
		} else {
			assert(false);
		}
		result.targets = targeted.target;
		result.number_of_targets = 1;
		return result;
	}

	struct addrinfo hints = {};
	errno = 0;
	hints.ai_family = enforced_proto;
	hints.ai_socktype = SOCK_RAW;

	int error;
	struct addrinfo *res;
	if ((error = getaddrinfo(arg, NULL, &hints, &res)) != 0) {
		errno = 0;
		crash("Failed to resolve %s: %s", arg, gai_strerror(error));
		result.error_code = ERROR;
		return result;
	}

	/* possibly add all the IP's as targets */
	for (struct addrinfo *address = res; address != NULL; address = address->ai_next) {
		struct sockaddr_storage temporary_ip_address;
		memcpy(&temporary_ip_address, address->ai_addr, address->ai_addrlen);

		add_target_ip_wrapper tmp = add_target_ip(temporary_ip_address);

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
			if (address->ai_family == AF_INET) {
				result.has_v4 = true;
			} else if (address->ai_family == AF_INET6) {
				result.has_v6 = true;
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

static void set_source_ip(char *arg, const int icmp_sock, sa_family_t addr_family) {
	struct sockaddr_in src;

	memset(&src, 0, sizeof(src));
	src.sin_family = addr_family;
	if ((src.sin_addr.s_addr = inet_addr(arg)) == INADDR_NONE) {
		src.sin_addr.s_addr = get_ip_address(arg, icmp_sock);
	}
	if (bind(icmp_sock, (struct sockaddr *)&src, sizeof(src)) == -1) {
		crash("Cannot bind to IP address %s", arg);
	}
}

/* TODO: Move this to netutils.c and also change check_dhcp to use that. */
static in_addr_t get_ip_address(const char *ifname, const int icmp_sock) {
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

	memcpy(&ip_address, &ifr.ifr_addr, sizeof(ip_address));
#else
	// fake operation to make the compiler happy
	(void)icmp_sock;

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
static get_timevar_wrapper get_timevar(const char *str) {
	get_timevar_wrapper result = {
		.error_code = OK,
		.time_range = 0,
	};

	if (!str) {
		result.error_code = ERROR;
		return result;
	}

	size_t len = strlen(str);
	if (!len) {
		result.error_code = ERROR;
		return result;
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
		result.time_range = (unsigned int)(pre_radix * factor);
		return result;
	}

	/* time specified in usecs can't have decimal points, so ignore them */
	if (factor == 1) {
		result.time_range = (unsigned int)pre_radix;
		return result;
	}

	/* integer and decimal, respectively */
	unsigned int post_radix = (unsigned int)strtoul(ptr + 1, NULL, 0);

	/* d is decimal, so get rid of excess digits */
	while (post_radix >= factor) {
		post_radix /= 10;
	}

	/* the last parenthesis avoids floating point exceptions. */
	result.time_range = (unsigned int)((pre_radix * factor) + (post_radix * (factor / 10)));
	return result;
}

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

	get_timevar_wrapper parsed_time = get_timevar(str);

	if (parsed_time.error_code == OK) {
		result.threshold.rta = parsed_time.time_range;
	} else {
		if (debug > 1) {
			printf("%s: failed to parse rta threshold\n", __FUNCTION__);
		}
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
		sum += *((uint8_t *)packet);
	}

	sum = (sum >> 16) + (sum & 0xffff); /* add hi 16 to low 16 */
	sum += (sum >> 16);                 /* add carry */
	unsigned short cksum;
	cksum = (unsigned short)~sum; /* ones-complement, trunc to 16 bits */

	return cksum;
}

void print_help(void) {
	// print_revision (progname); /* FIXME: Why? */
	printf("Copyright (c) 2005 Andreas Ericsson <ae@op5.se>\n");

	printf(COPYRIGHT, copyright, email);

	print_usage();

	printf(UT_HELP_VRSN);
	printf(UT_EXTRA_OPTS);

	printf(" -H, --Host=HOST\n");
	printf("    %s\n",
		   _("specify a target, might be one of: resolveable name | IPv6 address | IPv4 address\n"
			 "    (required, can be given multiple times)"));
	printf(" %s\n", "[-4|-6], [--ipv4-only|--ipv6-only]");
	printf("    %s\n", _("Use IPv4 or IPv6 only to communicate with the targets"));
	printf(" %s\n", "-w, --warning=WARN_VALUE");
	printf("    %s", _("warning threshold (default "));
	printf("%0.3fms,%u%%)\n", (float)DEFAULT_WARN_RTA / 1000, DEFAULT_WARN_PL);
	printf(" %s\n", "-c, --critical=CRIT_VALUE");
	printf("    %s", _("critical threshold (default "));
	printf("%0.3fms,%u%%)\n", (float)DEFAULT_CRIT_RTA / 1000, DEFAULT_CRIT_PL);

	printf(" %s\n", "-R, --rta-mode-thresholds=RTA_THRESHOLDS");
	printf("    %s\n",
		   _("RTA (round trip average) mode  warning,critical, ex. 100ms,200ms unit in ms"));
	printf(" %s\n", "-P, --packet-loss-mode-thresholds=PACKET_LOSS_THRESHOLD");
	printf("    %s\n", _("packet loss mode, ex. 40%,50% , unit in %"));
	printf(" %s\n", "-J, --jitter-mode-thresholds=JITTER_MODE_THRESHOLD");
	printf("    %s\n", _("jitter mode  warning,critical, ex. 40.000ms,50.000ms , unit in ms "));
	printf(" %s\n", "-M, --mos-mode-thresholds=MOS_MODE_THRESHOLD");
	printf("    %s\n", _("MOS mode, between 0 and 4.4  warning,critical, ex. 3.5,3.0"));
	printf(" %s\n", "-S, --score-mode-thresholds=SCORE_MODE_THRESHOLD");
	printf("    %s\n", _("score  mode, max value 100  warning,critical, ex. 80,70 "));
	printf(" %s\n", "-O, --out-of-order-packets");
	printf(
		"    %s\n",
		_("detect out of order ICMP packets, if such packets are found, the result is CRITICAL"));
	printf(" %s\n", "[-n|-p], --number-of-packets=NUMBER_OF_PACKETS");
	printf("    %s", _("number of packets to send (default "));
	printf("%u)\n", DEFAULT_NUMBER_OF_PACKETS);

	printf(" %s\n", "-i");
	printf("    %s", _("[DEPRECATED] packet interval (default "));
	printf("%0.3fms)\n", (float)DEFAULT_PKT_INTERVAL / 1000);
	printf("    %s", _("This option was never actually used and is just mentioned here for "
					   "historical purposes\n"));

	printf(" %s\n", "-I, --target-interval=TARGET_INTERVAL");
	printf("    %s%0.3fms)\n    The time interval to wait in between one target and the next\n",
		   _("max target interval (default "), (float)DEFAULT_TARGET_INTERVAL / 1000);
	printf(" %s\n", "-m, --minimal-host-alive=MIN_ALIVE");
	printf("    %s", _("number of alive hosts required for success. If less than MIN_ALIVE hosts "
					   "are OK, but MIN_ALIVE hosts are WARNING or OK, WARNING, else CRITICAL"));
	printf("\n");
	printf(" %s\n", "-l, --outgoing-ttl=OUTGOING_TTL");
	printf("    %s", _("TTL on outgoing packets (default "));
	printf("%u)\n", DEFAULT_TTL);
	printf(" %s\n", "-b, --size=SIZE");
	printf("    %s\n", _("Number of icmp ping data bytes to send"));
	printf("    %s %lu + %d)\n", _("Packet size will be SIZE + icmp header (default"),
		   DEFAULT_PING_DATA_SIZE, ICMP_MINLEN);
	printf(" %s\n", "-v, --verbose");
	printf("    %s\n", _("Verbosity, can be given multiple times (for debugging)"));

	printf(UT_OUTPUT_FORMAT);

	printf("\n");
	printf("%s\n", _("Notes:"));
	printf(" %s\n", _("If none of R,P,J,M,S or O is specified, default behavior is -R -P"));
	printf(" %s\n", _("Naming a host (or several) to check is not."));
	printf("\n");
	printf(" %s\n", _("Threshold format for -w and -c is 200.25,60% for 200.25 msec RTA and 60%"));
	printf(" %s\n", _("packet loss.  The default values should work well for most users."));
	printf(" %s\n",
		   _("You can specify different RTA factors using the standardized abbreviations"));
	printf(" %s\n",
		   _("us (microseconds), ms (milliseconds, default) or just plain s for seconds."));

	printf(UT_SUPPORT);
}

void print_usage(void) {
	printf("%s\n", _("Usage:"));
	printf(" %s [options] [-H host1 [-H host2 [-H hostN]]]\n", progname);
}

static add_host_wrapper add_host(char *arg, check_icmp_execution_mode mode,
								 sa_family_t enforced_proto) {
	if (debug) {
		printf("add_host called with argument %s\n", arg);
	}

	add_host_wrapper result = {
		.error_code = OK,
		.host = check_icmp_target_container_init(),
		.has_v4 = false,
		.has_v6 = false,
	};

	add_target_wrapper targets = add_target(arg, mode, enforced_proto);

	if (targets.error_code != OK) {
		result.error_code = targets.error_code;
		return result;
	}

	result.has_v4 = targets.has_v4;
	result.has_v6 = targets.has_v6;

	result.host = check_icmp_target_container_init();

	result.host.name = strdup(arg);
	result.host.target_list = targets.targets;
	result.host.number_of_targets = targets.number_of_targets;

	return result;
}

mp_subcheck evaluate_target(ping_target target, check_icmp_mode_switches modes,
							check_icmp_threshold warn, check_icmp_threshold crit) {
	/* if no new mode selected, use old schema */
	if (!modes.rta_mode && !modes.pl_mode && !modes.jitter_mode && !modes.score_mode &&
		!modes.mos_mode && !modes.order_mode) {
		modes.rta_mode = true;
		modes.pl_mode = true;
	}

	mp_subcheck result = mp_subcheck_init();
	result = mp_set_subcheck_default_state(result, STATE_OK);

	char address[INET6_ADDRSTRLEN];
	memset(address, 0, INET6_ADDRSTRLEN);
	parse_address(&target.address, address, sizeof(address));

	xasprintf(&result.output, "%s", address);

	double packet_loss;
	time_t rta;
	if (!target.icmp_recv) {
		/* rta 0 is of course not entirely correct, but will still show up
		 * conspicuously as missing entries in perfparse and cacti */
		packet_loss = 100;
		rta = 0;
		result = mp_set_subcheck_state(result, STATE_CRITICAL);
		/* up the down counter if not already counted */

		if (target.flags & FLAG_LOST_CAUSE) {
			xasprintf(&result.output, "%s: %s @ %s", result.output,
					  get_icmp_error_msg(target.icmp_type, target.icmp_code), address);
		} else { /* not marked as lost cause, so we have no flags for it */
			xasprintf(&result.output, "%s", result.output);
		}
	} else {
		packet_loss =
			(unsigned char)((target.icmp_sent - target.icmp_recv) * 100) / target.icmp_sent;
		rta = target.time_waited / target.icmp_recv;
	}

	double EffectiveLatency;
	double mos;   /* Mean opinion score */
	double score; /* score */

	if (target.icmp_recv > 1) {
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
		target.jitter = (target.jitter / (target.icmp_recv - 1) / 1000);

		/*
		 * Take the average round trip latency (in milliseconds), add
		 * round trip jitter, but double the impact to latency
		 * then add 10 for protocol latencies (in milliseconds).
		 */
		EffectiveLatency = ((double)rta / 1000) + (target.jitter * 2) + 10;

		double R;
		if (EffectiveLatency < 160) {
			R = 93.2 - (EffectiveLatency / 40);
		} else {
			R = 93.2 - ((EffectiveLatency - 120) / 10);
		}

		// Now, let us deduct 2.5 R values per percentage of packet loss (i.e. a
		// loss of 5% will be entered as 5).
		R = R - (packet_loss * 2.5);

		if (R < 0) {
			R = 0;
		}

		score = R;
		mos = 1 + ((0.035) * R) + ((.000007) * R * (R - 60) * (100 - R));
	} else {
		target.jitter = 0;
		target.jitter_min = 0;
		target.jitter_max = 0;
		mos = 0;
	}

	/* Check which mode is on and do the warn / Crit stuff */
	if (modes.rta_mode) {
		mp_subcheck sc_rta = mp_subcheck_init();
		sc_rta = mp_set_subcheck_default_state(sc_rta, STATE_OK);
		xasprintf(&sc_rta.output, "rta %0.3fms", (double)rta / 1000);

		if (rta >= crit.rta) {
			sc_rta = mp_set_subcheck_state(sc_rta, STATE_CRITICAL);
			xasprintf(&sc_rta.output, "%s >= %0.3fms", sc_rta.output, (double)crit.rta / 1000);
		} else if (rta >= warn.rta) {
			sc_rta = mp_set_subcheck_state(sc_rta, STATE_WARNING);
			xasprintf(&sc_rta.output, "%s >= %0.3fms", sc_rta.output, (double)warn.rta / 1000);
		}

		if (packet_loss < 100) {
			mp_perfdata pd_rta = perfdata_init();
			xasprintf(&pd_rta.label, "%srta", address);
			pd_rta.uom = strdup("ms");
			pd_rta.value = mp_create_pd_value(rta / 1000);
			pd_rta.min = mp_create_pd_value(0);

			pd_rta.warn = mp_range_set_end(pd_rta.warn, mp_create_pd_value(warn.rta));
			pd_rta.crit = mp_range_set_end(pd_rta.crit, mp_create_pd_value(crit.rta));
			mp_add_perfdata_to_subcheck(&sc_rta, pd_rta);

			mp_perfdata pd_rt_min = perfdata_init();
			xasprintf(&pd_rt_min.label, "%srtmin", address);
			pd_rt_min.value = mp_create_pd_value(target.rtmin / 1000);
			pd_rt_min.uom = strdup("ms");
			mp_add_perfdata_to_subcheck(&sc_rta, pd_rt_min);

			mp_perfdata pd_rt_max = perfdata_init();
			xasprintf(&pd_rt_max.label, "%srtmax", address);
			pd_rt_max.value = mp_create_pd_value(target.rtmax / 1000);
			pd_rt_max.uom = strdup("ms");
			mp_add_perfdata_to_subcheck(&sc_rta, pd_rt_max);
		}

		mp_add_subcheck_to_subcheck(&result, sc_rta);
	}

	if (modes.pl_mode) {
		mp_subcheck sc_pl = mp_subcheck_init();
		sc_pl = mp_set_subcheck_default_state(sc_pl, STATE_OK);
		xasprintf(&sc_pl.output, "packet loss %.1f%%", packet_loss);

		if (packet_loss >= crit.pl) {
			sc_pl = mp_set_subcheck_state(sc_pl, STATE_CRITICAL);
			xasprintf(&sc_pl.output, "%s >= %u%%", sc_pl.output, crit.pl);
		} else if (packet_loss >= warn.pl) {
			sc_pl = mp_set_subcheck_state(sc_pl, STATE_WARNING);
			xasprintf(&sc_pl.output, "%s >= %u%%", sc_pl.output, warn.pl);
		}

		mp_perfdata pd_pl = perfdata_init();
		xasprintf(&pd_pl.label, "%spl", address);
		pd_pl.uom = strdup("%");

		pd_pl.warn = mp_range_set_end(pd_pl.warn, mp_create_pd_value(warn.pl));
		pd_pl.crit = mp_range_set_end(pd_pl.crit, mp_create_pd_value(crit.pl));
		pd_pl.value = mp_create_pd_value(packet_loss);

		mp_add_perfdata_to_subcheck(&sc_pl, pd_pl);

		mp_add_subcheck_to_subcheck(&result, sc_pl);
	}

	if (modes.jitter_mode) {
		mp_subcheck sc_jitter = mp_subcheck_init();
		sc_jitter = mp_set_subcheck_default_state(sc_jitter, STATE_OK);
		xasprintf(&sc_jitter.output, "jitter %0.3fms", target.jitter);

		if (target.jitter >= crit.jitter) {
			sc_jitter = mp_set_subcheck_state(sc_jitter, STATE_CRITICAL);
			xasprintf(&sc_jitter.output, "%s >= %0.3fms", sc_jitter.output, crit.jitter);
		} else if (target.jitter >= warn.jitter) {
			sc_jitter = mp_set_subcheck_state(sc_jitter, STATE_WARNING);
			xasprintf(&sc_jitter.output, "%s >= %0.3fms", sc_jitter.output, warn.jitter);
		}

		if (packet_loss < 100) {
			mp_perfdata pd_jitter = perfdata_init();
			pd_jitter.uom = strdup("ms");
			xasprintf(&pd_jitter.label, "%sjitter_avg", address);
			pd_jitter.value = mp_create_pd_value(target.jitter);
			pd_jitter.warn = mp_range_set_end(pd_jitter.warn, mp_create_pd_value(warn.jitter));
			pd_jitter.crit = mp_range_set_end(pd_jitter.crit, mp_create_pd_value(crit.jitter));
			mp_add_perfdata_to_subcheck(&sc_jitter, pd_jitter);

			mp_perfdata pd_jitter_min = perfdata_init();
			pd_jitter_min.uom = strdup("ms");
			xasprintf(&pd_jitter_min.label, "%sjitter_min", address);
			pd_jitter_min.value = mp_create_pd_value(target.jitter_min);
			mp_add_perfdata_to_subcheck(&sc_jitter, pd_jitter_min);

			mp_perfdata pd_jitter_max = perfdata_init();
			pd_jitter_max.uom = strdup("ms");
			xasprintf(&pd_jitter_max.label, "%sjitter_max", address);
			pd_jitter_max.value = mp_create_pd_value(target.jitter_max);
			mp_add_perfdata_to_subcheck(&sc_jitter, pd_jitter_max);
		}
		mp_add_subcheck_to_subcheck(&result, sc_jitter);
	}

	if (modes.mos_mode) {
		mp_subcheck sc_mos = mp_subcheck_init();
		sc_mos = mp_set_subcheck_default_state(sc_mos, STATE_OK);
		xasprintf(&sc_mos.output, "MOS %0.1f", mos);

		if (mos <= crit.mos) {
			sc_mos = mp_set_subcheck_state(sc_mos, STATE_CRITICAL);
			xasprintf(&sc_mos.output, "%s <= %0.1f", sc_mos.output, crit.mos);
		} else if (mos <= warn.mos) {
			sc_mos = mp_set_subcheck_state(sc_mos, STATE_WARNING);
			xasprintf(&sc_mos.output, "%s <= %0.1f", sc_mos.output, warn.mos);
		}

		if (packet_loss < 100) {
			mp_perfdata pd_mos = perfdata_init();
			xasprintf(&pd_mos.label, "%smos", address);
			pd_mos.value = mp_create_pd_value(mos);
			pd_mos.warn = mp_range_set_end(pd_mos.warn, mp_create_pd_value(warn.mos));
			pd_mos.crit = mp_range_set_end(pd_mos.crit, mp_create_pd_value(crit.mos));
			pd_mos.min = mp_create_pd_value(0); // MOS starts at 0
			pd_mos.max = mp_create_pd_value(5); // MOS max is 5, by definition
			mp_add_perfdata_to_subcheck(&sc_mos, pd_mos);
		}
		mp_add_subcheck_to_subcheck(&result, sc_mos);
	}

	if (modes.score_mode) {
		mp_subcheck sc_score = mp_subcheck_init();
		sc_score = mp_set_subcheck_default_state(sc_score, STATE_OK);

		if (target.icmp_recv > 1) {
			xasprintf(&sc_score.output, "Score %f", score);

			if (score <= crit.score) {
				sc_score = mp_set_subcheck_state(sc_score, STATE_CRITICAL);
				xasprintf(&sc_score.output, "%s <= %f", sc_score.output, crit.score);
			} else if (score <= warn.score) {
				sc_score = mp_set_subcheck_state(sc_score, STATE_WARNING);
				xasprintf(&sc_score.output, "%s <= %f", sc_score.output, warn.score);
			}

			if (packet_loss < 100) {
				mp_perfdata pd_score = perfdata_init();
				xasprintf(&pd_score.label, "%sscore", address);
				pd_score.value = mp_create_pd_value(score);
				pd_score.warn = mp_range_set_end(pd_score.warn, mp_create_pd_value(warn.score));
				pd_score.crit = mp_range_set_end(pd_score.crit, mp_create_pd_value(crit.score));
				pd_score.min = mp_create_pd_value(0);
				pd_score.max = mp_create_pd_value(100);
				mp_add_perfdata_to_subcheck(&sc_score, pd_score);
			}

		} else {
			// score mode disabled due to not enough received packages
			xasprintf(&sc_score.output, "Score mode disabled, not enough packets received");
		}

		mp_add_subcheck_to_subcheck(&result, sc_score);
	}

	if (modes.order_mode) {
		mp_subcheck sc_order = mp_subcheck_init();
		sc_order = mp_set_subcheck_default_state(sc_order, STATE_OK);

		if (target.found_out_of_order_packets) {
			mp_set_subcheck_state(sc_order, STATE_CRITICAL);
			xasprintf(&sc_order.output, "Packets out of order");
		} else {
			xasprintf(&sc_order.output, "Packets in order");
		}

		mp_add_subcheck_to_subcheck(&result, sc_order);
	}

	return result;
}

evaluate_host_wrapper evaluate_host(check_icmp_target_container host,
									check_icmp_mode_switches modes, check_icmp_threshold warn,
									check_icmp_threshold crit) {
	evaluate_host_wrapper result = {
		.targets_warn = 0,
		.targets_ok = 0,
		.sc_host = mp_subcheck_init(),
	};
	result.sc_host = mp_set_subcheck_default_state(result.sc_host, STATE_OK);

	result.sc_host.output = strdup(host.name);

	ping_target *target = host.target_list;
	for (unsigned int i = 0; i < host.number_of_targets; i++) {
		mp_subcheck sc_target = evaluate_target(*target, modes, warn, crit);

		mp_state_enum target_state = mp_compute_subcheck_state(sc_target);

		if (target_state == STATE_WARNING) {
			result.targets_warn++;
		} else if (target_state == STATE_OK) {
			result.targets_ok++;
		}
		mp_add_subcheck_to_subcheck(&result.sc_host, sc_target);

		target = target->next;
	}

	return result;
}
