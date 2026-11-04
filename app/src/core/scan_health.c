/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <limits.h>

#include "scan_health.h"

/*
 * Public interface: documented in scan_health.h.
 */
void scan_health_reset(struct scan_health *h)
{
	h->failures = 0;
}

/*
 * Public interface: documented in scan_health.h.
 *
 * The count stops at UINT_MAX rather than wrapping, so a long run of failures with no limit
 * can't wrap round to zero.
 */
bool scan_health_record(struct scan_health *h, int ret, unsigned int limit)
{
	if (ret == 0) {
		h->failures = 0;
		return false;
	}
	if (h->failures < UINT_MAX) {
		h->failures++;
	}

	return limit != 0U && h->failures >= limit;
}
