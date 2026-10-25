/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "ap_history.h"

/*
 * Public interface: documented in ap_history.h.
 */
void ap_history_reset(struct ap_history *h)
{
	memset(h, 0, sizeof(*h));
}

/**
 * @brief Find an access point by BSSID.
 *
 * @param h History.
 * @param bssid BSSID.
 * @return Its index, or h->count if it is not there.
 *
 * @sideeffects None; errno unchanged.
 */
static size_t find(const struct ap_history *h, const uint8_t *bssid)
{
	size_t i = 0;

	while (i < h->count && memcmp(h->aps[i].bssid, bssid, CHAN_BSSID_LEN) != 0) {
		i++;
	}

	return i;
}

/**
 * @brief Find the access point heard longest ago.
 *
 * @param h History, not empty.
 * @return Its index.
 *
 * @sideeffects None; errno unchanged.
 */
static size_t oldest(const struct ap_history *h)
{
	size_t old = 0;

	for (size_t i = 1; i < h->count; i++) {
		if (h->heard_ms[i] < h->heard_ms[old]) {
			old = i;
		}
	}

	return old;
}

/*
 * Public interface: documented in ap_history.h.
 *
 * When the history is full, the slot of the access point heard longest ago (oldest()) is
 * reused; one heard in this same scan is never replaced.
 */
void ap_history_merge(struct ap_history *h, const struct chan_stats *scan, int64_t now_ms)
{
	for (size_t i = 0; i < scan->ap_count; i++) {
		size_t slot = find(h, scan->aps[i].bssid);

		if (slot == h->count) {
			if (h->count < CHAN_MAX_APS) {
				h->count++;
			} else {
				slot = oldest(h);
				if (h->heard_ms[slot] >= now_ms) {
					continue; /* full of this scan's own access points */
				}
			}
		}
		h->aps[slot] = scan->aps[i];
		h->aps[slot].freshness = CHAN_FRESH;
		h->heard_ms[slot] = now_ms;
	}
	h->ignored = scan->ignored;
	h->dropped = scan->dropped;
}

/*
 * Public interface: documented in ap_history.h.
 *
 * Compacts the arrays in place, keeping their order.
 */
void ap_history_expire(struct ap_history *h, int64_t now_ms, const struct ap_fade *fade)
{
	size_t kept = 0;

	for (size_t i = 0; i < h->count; i++) {
		if (now_ms - h->heard_ms[i] < fade->end_ms) {
			h->aps[kept] = h->aps[i];
			h->heard_ms[kept] = h->heard_ms[i];
			kept++;
		}
	}
	h->count = kept;
}

/*
 * Public interface: documented in ap_history.h.
 *
 * The products fit easily in int64_t. The end <= start case is caught by the second test, so
 * the division never divides by zero.
 */
uint8_t ap_history_freshness(int64_t age_ms, const struct ap_fade *fade)
{
	if (age_ms <= fade->start_ms) {
		return CHAN_FRESH;
	}
	if (age_ms >= fade->end_ms) {
		return 0; /* also when end_ms <= start_ms: no fade, so no division by 0 */
	}

	return (uint8_t)(((fade->end_ms - age_ms) * CHAN_FRESH) / (fade->end_ms - fade->start_ms));
}

/*
 * Public interface: documented in ap_history.h.
 */
void ap_history_snapshot(const struct ap_history *h, int64_t now_ms, const struct ap_fade *fade,
			 struct chan_stats *out)
{
	chan_stats_reset(out);
	for (size_t i = 0; i < h->count; i++) {
		uint8_t fresh = ap_history_freshness(now_ms - h->heard_ms[i], fade);

		if (fresh > 0U) {
			out->aps[out->ap_count] = h->aps[i];
			out->aps[out->ap_count].freshness = fresh;
			out->ap_count++;
		}
	}
	out->ignored = h->ignored;
	out->dropped = h->dropped;
}
