/**
 * @file p2p_tc.c
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
#include <errno.h>

#include "port.h"
#include "port_private.h"
#include "print.h"
#include "rtnl.h"
#include "tc.h"

static int p2p_delay_request(struct port *p)
{
	switch (p->state) {
	case PS_INITIALIZING:
	case PS_FAULTY:
	case PS_DISABLED:
		return 0;
	case PS_LISTENING:
	case PS_PRE_MASTER:
	case PS_MASTER:
	case PS_PASSIVE:
	case PS_UNCALIBRATED:
	case PS_SLAVE:
	case PS_GRAND_MASTER:
		break;
	}
	return port_delay_request(p);
}

void p2p_dispatch(struct port *p, enum fsm_event event, int mdiff)
{
	if (!port_state_update(p, event, mdiff)) {
		return;
	}
	/*
	 * Handle the side effects of the state transition.
	 */
	switch (p->state) {
	case PS_INITIALIZING:
		break;
	case PS_FAULTY:
	case PS_DISABLED:
		port_disable(p);
		break;
	case PS_LISTENING:
		port_clr_tmo(p->fda.fd[FD_ANNOUNCE_TIMER]);
		/* Set peer delay timer, but not on the UDS port. */
		if (portnum(p)) {
			port_set_delay_tmo(p);
		}
		break;
	case PS_PRE_MASTER:
	case PS_MASTER:
	case PS_GRAND_MASTER:
	case PS_PASSIVE:
	case PS_UNCALIBRATED:
	case PS_SLAVE:
		break;
	};
}

enum fsm_event p2p_event(struct port *p, int fd_index)
{
	enum fsm_event event = EV_NONE;
	struct ptp_message *msg;
	int cnt, fd = p->fda.fd[fd_index], err;

	switch (fd_index) {
	case FD_ANNOUNCE_TIMER:
	case FD_SYNC_RX_TIMER:
		pr_err("unexpected timer expiration");
		return EV_NONE;
	case FD_DELAY_TIMER:
		pr_debug("port %hu: delay timeout", portnum(p));
		port_set_delay_tmo(p);
		tc_prune(p);
		return p2p_delay_request(p) ? EV_FAULT_DETECTED : EV_NONE;
	case FD_QUALIFICATION_TIMER:
	case FD_MANNO_TIMER:
	case FD_SYNC_TX_TIMER:
		pr_err("unexpected timer expiration");
		return EV_NONE;
	case FD_RTNL:
		pr_debug("port %hu: received link status notification", portnum(p));
		rtnl_link_status(fd, port_link_status, p);
		return port_link_status_get(p) ? EV_FAULT_CLEARED : EV_FAULT_DETECTED;
	}

	msg = msg_allocate();
	if (!msg)
		return EV_FAULT_DETECTED;

	msg->hwts.type = p->timestamping;

	cnt = transport_recv(p->trp, fd, msg);
	if (cnt <= 0) {
		pr_err("port %hu: recv message failed", portnum(p));
		msg_put(msg);
		return EV_FAULT_DETECTED;
	}
	err = msg_post_recv(msg, cnt);
	if (err) {
		switch (err) {
		case -EBADMSG:
			pr_err("port %hu: bad message", portnum(p));
			break;
		case -ETIME:
			pr_err("port %hu: received %s without timestamp",
				portnum(p), msg_type_string(msg_type(msg)));
			break;
		case -EPROTO:
			pr_debug("port %hu: ignoring message", portnum(p));
			break;
		}
		msg_put(msg);
		return EV_NONE;
	}
	if (msg_sots_valid(msg)) {
		ts_add(&msg->hwts.ts, -p->rx_timestamp_offset);
	}
	if (msg->header.flagField[0] & UNICAST) {
		pl_warning(600, "cannot handle unicast messages!");
		goto out;
	}

	switch (msg_type(msg)) {
	case SYNC:
		if (tc_fwd_event(p, msg))
			event = EV_FAULT_DETECTED;
		break;
	case DELAY_REQ:
		break;
	case PDELAY_REQ:
		if (process_pdelay_req(p, msg))
			event = EV_FAULT_DETECTED;
		break;
	case PDELAY_RESP:
		if (process_pdelay_resp(p, msg))
			event = EV_FAULT_DETECTED;
		break;
	case FOLLOW_UP:
		if (tc_fwd_folup(p, msg))
			event = EV_FAULT_DETECTED;
		break;
	case DELAY_RESP:
		break;
	case PDELAY_RESP_FOLLOW_UP:
		process_pdelay_resp_fup(p, msg);
		break;
	case ANNOUNCE:
	case SIGNALING:
	case MANAGEMENT:
		if (tc_forward(p, msg))
			event = EV_FAULT_DETECTED;
		break;
	}
out:
	msg_put(msg);
	return event;
}
