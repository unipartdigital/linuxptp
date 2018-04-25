/**
 * @file ewma.c
 * @note Copyright (C) 2018 Petri Mattila <petri.mattila@unipart.io>
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
#include <string.h>
#include <stdint.h>

#include "ewma.h"
#include "filter_private.h"

struct ewma {
	struct filter filter;
	int div;
	int cnt;
	tmv_t sum;
};

static void ewma_destroy(struct filter *filter)
{
	struct ewma *m = container_of(filter, struct ewma, filter);
	free(m);
}

static tmv_t ewma_sample(struct filter *filter, tmv_t val)
{
	struct ewma *m = container_of(filter, struct ewma, filter);
	tmv_t sum;
	int div;

	m->cnt++;

	div = (m->div > m->cnt) ? m->cnt : m->div;

	sum = tmv_sub(val, m->sum);
	sum = tmv_div(sum, div);
	sum = tmv_add(m->sum, sum);

	m->sum = sum;

	return sum;
}

static void ewma_reset(struct filter *filter)
{
	struct ewma *m = container_of(filter, struct ewma, filter);
	m->sum = tmv_zero();
	m->cnt = 0;
}

struct filter *ewma_create(int length)
{
	struct ewma *m;
	m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	m->div = length;
	m->cnt = 0;
	m->filter.destroy = ewma_destroy;
	m->filter.sample = ewma_sample;
	m->filter.reset = ewma_reset;
	return &m->filter;
}
