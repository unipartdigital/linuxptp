/**
 * @file port_private.h
 * @note Copyright (C) 2015 Richard Cochran <richardcochran@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef HAVE_PORT_PRIVATE_H
#define HAVE_PORT_PRIVATE_H

#include <sys/queue.h>

#include "clock.h"
#include "fsm.h"
#include "msg.h"
#include "tmv.h"

#define NSEC2SEC 1000000000LL

enum syfu_state {
	SF_EMPTY,
	SF_HAVE_SYNC,
	SF_HAVE_FUP,
};

struct nrate_estimator {
	double ratio;
	tmv_t origin1;
	tmv_t ingress1;
	unsigned int max_count;
	unsigned int count;
	int ratio_valid;
};

struct tc_txd {
	TAILQ_ENTRY(tc_txd) list;
	struct ptp_message *msg;
	tmv_t residence;
	int ingress_port;
};

struct port {
	LIST_ENTRY(port) list;
	char *name;
	struct clock *clock;
	struct transport *trp;
	enum timestamp_type timestamping;
	struct fdarray fda;
	int fault_fd;
	int phc_index;

	void (*dispatch)(struct port *p, enum fsm_event event, int mdiff);
	enum fsm_event (*event)(struct port *p, int fd_index);

	int jbod;
	struct foreign_clock *best;
	enum syfu_state syfu;
	struct ptp_message *last_syncfup;
	struct ptp_message *delay_req;
	struct ptp_message *peer_delay_req;
	struct ptp_message *peer_delay_resp;
	struct ptp_message *peer_delay_fup;
	int peer_portid_valid;
	struct PortIdentity peer_portid;
	struct {
		UInteger16 announce;
		UInteger16 delayreq;
		UInteger16 sync;
	} seqnum;
	tmv_t peer_delay;
	struct tsproc *tsproc;
	int log_sync_interval;
	struct nrate_estimator nrate;
	unsigned int pdr_missing;
	unsigned int multiple_seq_pdr_count;
	unsigned int multiple_pdr_detected;
	enum port_state (*state_machine)(enum port_state state,
					 enum fsm_event event, int mdiff);
	/* portDS */
	struct PortIdentity portIdentity;
	enum port_state     state; /*portState*/
	Integer64           asymmetry;
	int                 asCapable;
	Integer8            logMinDelayReqInterval;
	TimeInterval        peerMeanPathDelay;
	Integer8            logAnnounceInterval;
	UInteger8           announceReceiptTimeout;
	int                 announce_span;
	UInteger8           syncReceiptTimeout;
	UInteger8           transportSpecific;
	Integer8            logSyncInterval;
	Enumeration8        delayMechanism;
	Integer8            logMinPdelayReqInterval;
	UInteger32          neighborPropDelayThresh;
	int                 follow_up_info;
	int                 freq_est_interval;
	int                 hybrid_e2e;
	int                 min_neighbor_prop_delay;
	int                 path_trace_enabled;
	int                 rx_timestamp_offset;
	int                 tx_timestamp_offset;
	int                 link_status;
	struct fault_interval flt_interval_pertype[FT_CNT];
	enum fault_type     last_fault_type;
	unsigned int        versionNumber; /*UInteger4*/
	/* foreignMasterDS */
	LIST_HEAD(fm, foreign_clock) foreign_masters;
	/* TC book keeping */
	TAILQ_HEAD(tct, tc_txd) tc_transmitted;
};

#define portnum(p) (p->portIdentity.portNumber)

int clear_fault_asap(struct fault_interval *faint);
int port_clr_tmo(int fd);
int port_delay_request(struct port *p);
void port_disable(struct port *p);
int port_initialize(struct port *p);
int port_is_enabled(struct port *p);
void port_link_status(void *ctx, int index, int linkup);
int port_set_delay_tmo(struct port *p);
void port_show_transition(struct port *p, enum port_state next,
			  enum fsm_event event);
int process_pdelay_req(struct port *p, struct ptp_message *m);
int process_pdelay_resp(struct port *p, struct ptp_message *m);
void process_pdelay_resp_fup(struct port *p, struct ptp_message *m);
int source_pid_eq(struct ptp_message *m1, struct ptp_message *m2);
void ts_add(struct timespec *ts, int ns);

#endif
