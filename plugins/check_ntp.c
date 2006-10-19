/******************************************************************************
*
* Nagios check_ntp plugin
*
* License: GPL
* Copyright (c) 2006 sean finney <seanius@seanius.net>
* Copyright (c) 2006 nagios-plugins team
*
* Last Modified: $Date$
*
* Description:
*
* This file contains the check_ntp plugin
*
*  This plugin to check ntp servers independant of any commandline
*  programs or external libraries.
*
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
static double owarn=60;
static double ocrit=120;
static short do_jitter=0;
static double jwarn=5000;
static double jcrit=10000;

int process_arguments (int, char **);
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
} ntp_server_results;

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
/* In peer status bytes, bytes 6,7,8 determine clock selection status */
#define PEER_SEL(x) (x&0x07)
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
			R32(n)=htonl((4294.967296*t.tv_usec)+.5); \
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
			if (PEER_SEL(peer[i].status) >= PEER_INCLUDED){
				if(PEER_SEL(peer[i].status) >= PEER_SYNCSOURCE){
					printf(" <-- current sync source");
				} else {
					printf(" <-- current sync candidate");
				}
			}
			printf("\n");
		}
	}
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

/* select the "best" server from a list of servers, and return its index.
 * this is done by filtering servers based on stratum, dispersion, and
 * finally round-trip delay. */
int best_offset_server(const ntp_server_results *slist, int nservers){
	int i=0, j=0, cserver=0, candidates[5], csize=0;

	/* for each server */
	for(cserver=0; cserver<nservers; cserver++){
		/* compare it to each of the servers already in the candidate list */
		for(i=0; i<csize; i++){
			/* does it have an equal or better stratum? */
			if(slist[cserver].stratum <= slist[i].stratum){
				/* does it have an equal or better dispersion? */
				if(slist[cserver].rtdisp <= slist[i].rtdisp){
					/* does it have a better rtdelay? */
					if(slist[cserver].rtdelay < slist[i].rtdelay){
						break;
					}
				}
			}
		}

		/* if we haven't reached the current list's end, move everyone
		 * over one to the right, and insert the new candidate */
		if(i<csize){
			for(j=5; j>i; j--){
				candidates[j]=candidates[j-1];
			}
		}
		/* regardless, if they should be on the list... */
		if(i<5) {
			candidates[i]=cserver;
			if(csize<5) csize++;
		/* otherwise discard the server */
		} else {
			DBG(printf("discarding peer id %d\n", cserver));
		}
	}

	if(csize>0) {
		DBG(printf("best server selected: peer %d\n", candidates[0]));
		return candidates[0];
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
	ga_result = getaddrinfo(host, "123", &hints, &ai);
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

	/* setup each socket for writing, and the corresponding struct pollfd */
	ai_tmp=ai;
	for(i=0;ai_tmp;i++){
		socklist[i]=socket(ai_tmp->ai_family, SOCK_DGRAM, IPPROTO_UDP);
		if(socklist[i] == -1) {
			perror(NULL);
			die(STATE_UNKNOWN, "can not create new socket");
		}
		if(connect(socklist[i], ai_tmp->ai_addr, ai_tmp->ai_addrlen)){
			die(STATE_UNKNOWN, "can't create socket connection");
		} else {
			ufds[i].fd=socklist[i];
			ufds[i].events=POLLIN;
			ufds[i].revents=0;
		}
		ai_tmp = ai_tmp->ai_next;
	}

	/* now do AVG_NUM checks to each host.  we stop before timeout/2 seconds
	 * have passed in order to ensure post-processing and jitter time. */
	now_time=start_ts=time(NULL);
	while(servers_completed<num_hosts && now_time-start_ts <= socket_timeout/2){
		/* loop through each server and find each one which hasn't
		 * been touched in the past second or so and is still lacking
		 * some responses.  for each of these servers, send a new request,
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
		*status=STATE_CRITICAL;
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

/* XXX handle responses with the error bit set */
double jitter_request(const char *host, int *status){
	int conn=-1, i, npeers=0, num_candidates=0, syncsource_found=0;
	int run=0, min_peer_sel=PEER_INCLUDED, num_selected=0, num_valid=0;
	int peer_offset=0;
	ntp_assoc_status_pair *peers=NULL;
	ntp_control_message req;
	double rval = 0.0, jitter = -1.0;
	char *startofvalue=NULL, *nptr=NULL;

	/* Long-winded explanation:
	 * Getting the jitter requires a number of steps:
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
	 * 4) Extract the jitter value from the data[] (it's ASCII)
	 */
	my_udp_connect(server_address, 123, &conn);

	/* keep sending requests until the server stops setting the
	 * REM_MORE bit, though usually this is only 1 packet. */
	do{
		setup_control_request(&req, OP_READSTAT, 1);
		DBG(printf("sending READSTAT request"));
		write(conn, &req, SIZEOF_NTPCM(req));
		DBG(print_ntp_control_message(&req));
		/* Attempt to read the largest size packet possible */
		req.count=htons(MAX_CM_SIZE);
		DBG(printf("recieving READSTAT response"))
		read(conn, &req, SIZEOF_NTPCM(req));
		DBG(print_ntp_control_message(&req));
		/* Each peer identifier is 4 bytes in the data section, which
	 	 * we represent as a ntp_assoc_status_pair datatype.
	 	 */
		npeers+=(ntohs(req.count)/sizeof(ntp_assoc_status_pair));
		peers=(ntp_assoc_status_pair*)realloc(peers, sizeof(ntp_assoc_status_pair)*npeers);
		memcpy((void*)peers+peer_offset, (void*)req.data, sizeof(ntp_assoc_status_pair)*npeers);
		peer_offset+=ntohs(req.count);
	} while(req.op&REM_MORE);

	/* first, let's find out if we have a sync source, or if there are
	 * at least some candidates.  in the case of the latter we'll issue
	 * a warning but go ahead with the check on them. */
	for (i = 0; i < npeers; i++){
		if (PEER_SEL(peers[i].status) >= PEER_INCLUDED){
			num_candidates++;
			if(PEER_SEL(peers[i].status) >= PEER_SYNCSOURCE){
				syncsource_found=1;
				min_peer_sel=PEER_SYNCSOURCE;
			}
		}
	}
	if(verbose) printf("%d candiate peers available\n", num_candidates);
	if(verbose && syncsource_found) printf("synchronization source found\n");
	if(! syncsource_found) *status = STATE_WARNING;


	for (run=0; run<AVG_NUM; run++){
		if(verbose) printf("jitter run %d of %d\n", run+1, AVG_NUM);
		for (i = 0; i < npeers; i++){
			/* Only query this server if it is the current sync source */
			if (PEER_SEL(peers[i].status) >= min_peer_sel){
				num_selected++;
				setup_control_request(&req, OP_READVAR, 2);
				req.assoc = peers[i].assoc;
				/* By spec, putting the variable name "jitter"  in the request
				 * should cause the server to provide _only_ the jitter value.
				 * thus reducing net traffic, guaranteeing us only a single
				 * datagram in reply, and making intepretation much simpler
				 */
				strncpy(req.data, "jitter", 6);
				req.count = htons(6);
				DBG(printf("sending READVAR request...\n"));
				write(conn, &req, SIZEOF_NTPCM(req));
				DBG(print_ntp_control_message(&req));

				req.count = htons(MAX_CM_SIZE);
				DBG(printf("recieving READVAR response...\n"));
				read(conn, &req, SIZEOF_NTPCM(req));
				DBG(print_ntp_control_message(&req));

				/* get to the float value */
				if(verbose) {
					printf("parsing jitter from peer %.2x: ", peers[i].assoc);
				}
				startofvalue = strchr(req.data, '=') + 1;
				if(startofvalue != NULL) {
					jitter = strtod(startofvalue, &nptr);
				}
				if(startofvalue == NULL || startofvalue==nptr){
					printf("warning: unable to read server jitter response.\n");
					*status = STATE_WARNING;
				} else {
					if(verbose) printf("%g\n", jitter);
					num_valid++;
					rval += jitter;
				}
			}
		}
		if(verbose){
			printf("jitter parsed from %d/%d peers\n", num_valid, num_selected);
		}
	}

	rval /= num_valid;

	close(conn);
	free(peers);
	/* If we return -1.0, it means no synchronization source was found */
	return rval;
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
			verbose++;
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
	int result, offset_result, jitter_result;
	double offset=0, jitter=0;

	result=offset_result=jitter_result=STATE_UNKNOWN;

	if (process_arguments (argc, argv) == ERROR)
		usage4 (_("Could not parse arguments"));

	/* initialize alarm signal handling */
	signal (SIGALRM, socket_timeout_alarm_handler);

	/* set socket timeout */
	alarm (socket_timeout);

	offset = offset_request(server_address, &offset_result);
	if(fabs(offset) > ocrit){
		result = STATE_CRITICAL;
	} else if(fabs(offset) > owarn) {
		result = STATE_WARNING;
	} else {
		result = STATE_OK;
	}
	result=max_state(result, offset_result);

	/* If not told to check the jitter, we don't even send packets.
	 * jitter is checked using NTP control packets, which not all
	 * servers recognize.  Trying to check the jitter on OpenNTPD
	 * (for example) will result in an error
	 */
	if(do_jitter){
		jitter=jitter_request(server_address, &jitter_result);
		if(jitter > jcrit){
			result = max_state(result, STATE_CRITICAL);
		} else if(jitter > jwarn) {
			result = max_state(result, STATE_WARNING);
		} else if(jitter == -1.0 && result == STATE_OK){
			/* -1 indicates that we couldn't calculate the jitter
			 * Only overrides STATE_OK from the offset */
			result = STATE_UNKNOWN;
		}
	}
	result=max_state(result, jitter_result);

	switch (result) {
		case STATE_CRITICAL :
			printf("NTP CRITICAL: ");
			break;
		case STATE_WARNING :
			printf("NTP WARNING: ");
			break;
		case STATE_OK :
			printf("NTP OK: ");
			break;
		default :
			printf("NTP UNKNOWN: ");
			break;
	}
	if(offset_result==STATE_CRITICAL){
		printf("Offset unknown|offset=unknown");
	} else {
		if(offset_result==STATE_WARNING){
			printf("Unable to fully sample sync server. ");
		}
		printf("Offset %.10g secs|offset=%.10g", offset, offset);
	}
	if (do_jitter) printf(" jitter=%f", jitter);
	printf("\n");

	if(server_address!=NULL) free(server_address);
	return result;
}



void print_help(void){
	print_revision(progname, revision);

	printf ("Copyright (c) 2006 Sean Finney\n");
	printf (COPYRIGHT, copyright, email);
  
  printf ("%s\n", _("This plugin checks the selected ntp server"));

  printf ("\n\n");
  
	print_usage();
	printf (_(UT_HELP_VRSN));
	printf (_(UT_HOST_PORT), 'p', "123");
	printf (_(UT_WARN_CRIT));
	printf (_(UT_TIMEOUT), DEFAULT_SOCKET_TIMEOUT);
	printf (_(UT_VERBOSE));
	printf (_(UT_SUPPORT));
}

void
print_usage(void)
{
  printf (_("Usage:"));
  printf("%s -H <host> [-O] [-w <warn>] [-c <crit>] [-j <warn>] [-k <crit>] [-v verbose]\n", progname);
}
