/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Scans in a thread of their own, every CONFIG_METER_SCAN_INTERVAL_S
 * seconds, into a history of the access points heard (ap_history.c), so
 * the display can be redrawn, and faded, while a scan runs. A scan that
 * fails or hears nothing is retried after CONFIG_METER_RETRY_S seconds.
 */

#ifndef METER_SCAN_TASK_H_
#define METER_SCAN_TASK_H_

#include <stdbool.h>
#include <stdint.h>

#include "chan_stats.h"

/** What the scan thread has done. */
struct scan_task_state {
	unsigned int scans; /**< scans finished, failed or not */
	int last_error;     /**< 0, or the latest scan's negative errno */
	bool busy;          /**< a scan is running */
};

/**
 * @brief Start scanning.
 *
 * Call once, after scanner_init() and watchdog_init() have succeeded.
 *
 * @sideeffects Adds the scan thread's watchdog channel (@c scan_watch, SCAN_DEADLINE_MS) and
 *              starts the thread (@c scan_thread, named "scan"), which begins scanning at once.
 *              errno unchanged.
 */
void scan_task_start(void);

/**
 * @brief Start the next scan now, instead of when it is due.
 *
 * @sideeffects Gives @c scan_now, which wakes the scan thread early. errno unchanged.
 */
void scan_task_request(void);

/**
 * @brief Take the access points still showing at @p now_ms, faded by age.
 *
 * Only reads: faded-out access points are forgotten by the scan thread.
 * Safe to call from any thread.
 *
 * @param now_ms Time now (k_uptime_get()).
 * @param[out] out The access points, with their freshness.
 * @param[out] state The scan thread's progress.
 *
 * @sideeffects None (reads @c history and @c progress under @c lock; writes only through @p out
 *              and @p state); errno unchanged.
 */
void scan_task_snapshot(int64_t now_ms, struct chan_stats *out, struct scan_task_state *state);

#endif /* METER_SCAN_TASK_H_ */
