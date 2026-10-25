/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The access points heard over recent scans, each with the time it was last
 * heard, so that one scan that misses a network (or hears nothing at all)
 * doesn't blank the display. A network stays at full brightness for a
 * while after it was last heard, then fades out and is forgotten. No
 * Zephyr APIs here: times are passed in, in milliseconds.
 */

#ifndef METER_AP_HISTORY_H_
#define METER_AP_HISTORY_H_

#include <stddef.h>
#include <stdint.h>

#include "chan_stats.h"

/** Access points remembered, with when each was last heard. */
struct ap_history {
	struct chan_ap aps[CHAN_MAX_APS];
	int64_t heard_ms[CHAN_MAX_APS];
	size_t count;
	size_t ignored; /**< the latest scan's ignored results */
	size_t dropped; /**< the latest scan's dropped results */
};

/** How long access points stay shown. */
struct ap_fade {
	int64_t start_ms; /**< age at which an access point starts to fade */
	int64_t end_ms;   /**< age at which it has faded out and is forgotten */
};

/**
 * @brief Forget every access point.
 *
 * @param[out] h History.
 *
 * @sideeffects None (writes only through @p h); errno unchanged.
 */
void ap_history_reset(struct ap_history *h);

/**
 * @brief Add a scan's access points, heard at @p now_ms.
 *
 * Access points already known (by BSSID) take the scan's channel and
 * signal; new ones are added. When the history is full, a new access point
 * replaces the one heard longest ago, if that is older than @p now_ms.
 *
 * @param[in,out] h History.
 * @param scan The scan.
 * @param now_ms When the scan finished.
 *
 * @sideeffects None (writes only through @p h); errno unchanged.
 */
void ap_history_merge(struct ap_history *h, const struct chan_stats *scan, int64_t now_ms);

/**
 * @brief Forget the access points that have faded out.
 *
 * @param[in,out] h History.
 * @param now_ms Time now.
 * @param fade Fade timing.
 *
 * @sideeffects None (writes only through @p h); errno unchanged.
 */
void ap_history_expire(struct ap_history *h, int64_t now_ms, const struct ap_fade *fade);

/**
 * @brief Work out an access point's freshness from its age.
 *
 * @param age_ms Time since it was last heard.
 * @param fade Fade timing.
 * @return CHAN_FRESH up to fade->start_ms, falling in a straight line to 0
 *         at fade->end_ms (straight from CHAN_FRESH to 0 if end_ms is not
 *         after start_ms).
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t ap_history_freshness(int64_t age_ms, const struct ap_fade *fade);

/**
 * @brief Make statistics of the access points still showing, with their
 *        freshness at @p now_ms.
 *
 * @param h History.
 * @param now_ms Time now.
 * @param fade Fade timing.
 * @param[out] out The access points not yet faded out, and the latest
 *             scan's ignored and dropped counts.
 *
 * @sideeffects None (writes only through @p out); errno unchanged.
 */
void ap_history_snapshot(const struct ap_history *h, int64_t now_ms, const struct ap_fade *fade,
			 struct chan_stats *out);

#endif /* METER_AP_HISTORY_H_ */
