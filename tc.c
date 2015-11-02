/**
 * @file tc.c
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
#include <stdlib.h>

#include "port.h"
#include "print.h"
#include "tc.h"
#include "tmv.h"

enum tc_match {
	TC_MISMATCH,
	TC_SYNC_FUP,
	TC_FUP_SYNC,
};

static TAILQ_HEAD(tc_pool, tc_txd) tc_pool = TAILQ_HEAD_INITIALIZER(tc_pool);

static struct tc_txd *tc_allocate(void)
{
	struct tc_txd *txd = TAILQ_FIRST(&tc_pool);

	if (txd) {
		TAILQ_REMOVE(&tc_pool, txd, list);
		memset(txd, 0, sizeof(*txd));
		return txd;
	}
	txd = calloc(1, sizeof(*txd));
	return txd;
}

static int tc_blocked(struct port *p)
{
	if (portnum(p) == 0) {
		return 1;
	}
	enum port_state s = port_state(p);
	switch (s) {
	case PS_INITIALIZING:
	case PS_FAULTY:
	case PS_DISABLED:
	case PS_PASSIVE:
		break;
	case PS_LISTENING:
	case PS_PRE_MASTER:
	case PS_MASTER:
	case PS_UNCALIBRATED:
	case PS_SLAVE:
	case PS_GRAND_MASTER:
		return 0;
	}
	return 1;
}

static int tc_current(struct ptp_message *m, struct timespec now)
{
	int64_t t1, t2, tmo = 1LL * NSEC2SEC;
	t1 = m->ts.host.tv_sec * NSEC2SEC + m->ts.host.tv_nsec;
	t2 = now.tv_sec * NSEC2SEC + now.tv_nsec;
	return t2 - t1 < tmo;
}

static void tc_free(struct tc_txd *txd)
{
	TAILQ_INSERT_HEAD(&tc_pool, txd, list);
}

static int tc_match(int ingress_port, struct ptp_message *msg,
		    struct tc_txd *txd)
{
	if (ingress_port != txd->ingress_port) {
		return TC_MISMATCH;
	}
	if (msg->header.sequenceId != txd->msg->header.sequenceId) {
		return TC_MISMATCH;
	}
	if (!source_pid_eq(msg, txd->msg)) {
		return TC_MISMATCH;
	}
	if (msg_type(txd->msg) == SYNC && msg_type(msg) == FOLLOW_UP) {
		return TC_SYNC_FUP;
	}
	if (msg_type(txd->msg) == FOLLOW_UP && msg_type(msg) == SYNC) {
		return TC_FUP_SYNC;
	}
	return TC_MISMATCH;
}

static void tc_complete(struct port *q, struct port *p, struct ptp_message *msg,
			tmv_t residence)
{
	struct ptp_message *fup;
	struct tc_txd *txd;
	enum tc_match type = TC_MISMATCH;
	Integer64 c1, c2;
	int cnt;

	TAILQ_FOREACH(txd, &p->tc_transmitted, list) {
		type = tc_match(portnum(q), msg, txd);
		switch (type) {
		case TC_MISMATCH:
			break;
		case TC_SYNC_FUP:
			fup = msg;
			residence = txd->residence;
			break;
		case TC_FUP_SYNC:
			fup = txd->msg;
			break;
		}
		if (type != TC_MISMATCH) {
			break;
		}
	}

	if (type == TC_MISMATCH) {
		txd = tc_allocate();
		if (!txd) {
			pr_err("low memory, TC failed to forward event");
			port_dispatch(p, EV_FAULT_DETECTED, 0);
			return;
		}
		msg_get(msg);
		txd->msg = msg;
		txd->residence = residence;
		txd->ingress_port = port_number(q);
		TAILQ_INSERT_TAIL(&p->tc_transmitted, txd, list);
		return;
	}

	c1 = net2host64(fup->header.correction);
	c2 = c1 + tmv_to_TimeInterval(residence);
	fup->header.correction = host2net64(c2);
	cnt = transport_send(p->trp, &p->fda, 0, fup);
	if (cnt <= 0) {
		port_dispatch(p, EV_FAULT_DETECTED, 0);
	}
	TAILQ_REMOVE(&p->tc_transmitted, txd, list);
	msg_put(txd->msg);
	tc_free(txd);
}

/* public methods */

void tc_cleanup(void)
{
	struct tc_txd *txd;
	while ((txd = TAILQ_FIRST(&tc_pool)) != NULL) {
		TAILQ_REMOVE(&tc_pool, txd, list);
		free(txd);
	}
}

void tc_flush(struct port *q)
{
	struct tc_txd *txd;
	while ((txd = TAILQ_FIRST(&q->tc_transmitted)) != NULL) {
		TAILQ_REMOVE(&q->tc_transmitted, txd, list);
		msg_put(txd->msg);
		tc_free(txd);
	}
}

int tc_forward(struct port *q, struct ptp_message *msg)
{
	struct port *p;
	int cnt;

	if (msg_pre_send(msg)) {
		return -1;
	}
	for (p = clock_first_port(q->clock); p; p = LIST_NEXT(p, list)) {
		if (p == q || tc_blocked(p)) {
			continue;
		}
		cnt = transport_send(p->trp, &p->fda, 0, msg);
		if (cnt <= 0) {
			/* egress port is faulty. */
			port_dispatch(p, EV_FAULT_DETECTED, 0);
		}
	}
	return 0;
}

int tc_fwd_event(struct port *q, struct ptp_message *msg)
{
	tmv_t egress, ingress = timespec_to_tmv(msg->hwts.ts), residence;
	struct port *p;
	int cnt;

	clock_gettime(CLOCK_MONOTONIC, &msg->ts.host);
	if (msg_pre_send(msg)) {
		return -1;
	}
	for (p = clock_first_port(q->clock); p; p = LIST_NEXT(p, list)) {
		if (p == q || tc_blocked(p)) {
			continue;
		}
		cnt = transport_send(p->trp, &p->fda, 1, msg);
		if (cnt <= 0 || !msg_sots_valid(msg)) {
			port_dispatch(p, EV_FAULT_DETECTED, 0);
			continue;
		}
		ts_add(&msg->hwts.ts, p->tx_timestamp_offset);
		egress = timespec_to_tmv(msg->hwts.ts);
		residence = tmv_sub(egress, ingress);
		tc_complete(q, p, msg, residence);
	}
	return 0;
}

int tc_fwd_folup(struct port *q, struct ptp_message *msg)
{
	struct Timestamp *ts = &msg->follow_up.preciseOriginTimestamp;
	struct port *p;

	clock_gettime(CLOCK_MONOTONIC, &msg->ts.host);

	ts->seconds_lsb = msg->ts.pdu.sec & 0xFFFFFFFF;
	ts->seconds_msb = msg->ts.pdu.sec >> 32;
	ts->nanoseconds = msg->ts.pdu.nsec;

	if (msg_pre_send(msg)) {
		return -1;
	}
	for (p = clock_first_port(q->clock); p; p = LIST_NEXT(p, list)) {
		if (p == q || tc_blocked(p)) {
			continue;
		}
		tc_complete(q, p, msg, tmv_zero());
	}
	return 0;
}

void tc_prune(struct port *q)
{
	struct tc_txd *txd;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	while ((txd = TAILQ_FIRST(&q->tc_transmitted)) != NULL) {
		if (tc_current(txd->msg, now)) {
			break;
		}
		TAILQ_REMOVE(&q->tc_transmitted, txd, list);
		msg_put(txd->msg);
		tc_free(txd);
	}
}
