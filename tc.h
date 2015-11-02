/**
 * @file tc.h
 * @brief Implements a Transparent Clock.
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
#ifndef HAVE_TC_H
#define HAVE_TC_H

#include "msg.h"
#include "port_private.h"

void tc_flush(struct port *q);

int tc_forward(struct port *q, struct ptp_message *msg);

int tc_fwd_event(struct port *q, struct ptp_message *msg);

int tc_fwd_folup(struct port *q, struct ptp_message *msg);

void tc_prune(struct port *q);

#endif
