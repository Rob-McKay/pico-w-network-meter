/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/clock.h>
#include <zephyr/toolchain.h>

#include "ap_history.h"
#include "reset_note.h"
#include "scan_health.h"
#include "scan_task.h"
#include "recommend.h"
#include "scanner.h"
#include "watchdog.h"
#include "boot_record.h"
#include "chan_stats.h"

LOG_MODULE_REGISTER(scan_task, CONFIG_METER_LOG_LEVEL);

/* A margin on the scan thread's watchdog for the Wi-Fi driver's own waits, s: it waits up to
 * 30 s for its lock before giving up on a request.
 */
#define DRIVER_MARGIN_S 30U
/* The scan thread's watchdog deadline: the longest scan and wait, and the driver's margin. */
#define SCAN_DEADLINE_MS                                                                           \
	((CONFIG_METER_SCAN_TIMEOUT_S + MAX(CONFIG_METER_SCAN_INTERVAL_S, CONFIG_METER_RETRY_S) +  \
	  DRIVER_MARGIN_S) *                                                                       \
	 MSEC_PER_SEC)
/* CONFIG_METER_FADE_MIN_PERCENT's whole. */
#define PERCENT 100

/* A hidden network's name on the console: this, then its BSSID, as on the screen. */
#define HIDDEN_PREFIX "hidden "

/* Room for a signal level as text: "-127 dBm" and its terminator. */
#define DBM_TEXT_MAX 16U

BUILD_ASSERT(CONFIG_METER_FADE_END_S > CONFIG_METER_FADE_START_S,
	     "CONFIG_METER_FADE_END_S must be more than CONFIG_METER_FADE_START_S");

static const struct ap_fade fade = {
	.start_ms = (int64_t)CONFIG_METER_FADE_START_S * MSEC_PER_SEC,
	.end_ms = (int64_t)CONFIG_METER_FADE_END_S * MSEC_PER_SEC,
	.min_level = (uint8_t)((CONFIG_METER_FADE_MIN_PERCENT * CHAN_FRESH) / PERCENT),
};

/* Zephyr's definition macros can end in an empty statement: ISO C -Wpedantic
 * flags the ';' as extra. Nothing else is exempted.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static K_THREAD_STACK_DEFINE(scan_stack, CONFIG_METER_SCAN_STACK_SIZE);
static K_SEM_DEFINE(scan_now, 0, 1);
#pragma GCC diagnostic pop
static struct k_thread scan_thread;

/* The scan thread's watchdog channel: set before the thread starts. */
static int scan_watch = -1;

/* The scan in progress, and the failures in a row: only the scan thread touches them. */
static struct chan_stats scanning;
static struct scan_health health;

/* Shared with the display thread, under lock. */
static K_MUTEX_DEFINE(lock);
static struct ap_history history;
static struct scan_task_state progress;

/**
 * @brief Write a signal level for the console: "-72 dBm", or "clear" for
 *        CHAN_RSSI_NONE (nothing heard) rather than "-127 dBm".
 *
 * @param dbm dBm, or CHAN_RSSI_NONE.
 * @param[out] buf The text, cut to fit and always terminated.
 * @param size Size of @p buf, at least 1.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
static void dbm_text(int dbm, char *buf, size_t size)
{
	if (dbm == CHAN_RSSI_NONE) {
		(void)snprintf(buf, size, "clear");
	} else {
		(void)snprintf(buf, size, "%d dBm", dbm);
	}
}


/**
 * @brief Log one channel's figures, if anything reaches it.
 *
 * @param info The channel's figures.
 *
 * @sideeffects Writes a log message. errno may be set by snprintf() (only on an output
 *              error).
 */
static void report_channel(const struct chan_info *info)
{
	char strongest[DBM_TEXT_MAX];
	char others[DBM_TEXT_MAX];
	char total[DBM_TEXT_MAX];

	if (info->ap_count == 0U && info->interference == CHAN_RSSI_NONE) {
		return;
	}
	dbm_text(info->best_rssi, strongest, sizeof(strongest));
	dbm_text(info->others, others, sizeof(others));
	dbm_text(info->interference, total, sizeof(total));
	LOG_INF("  ch %2u: %u networks, strongest %8s, interference %8s, total %8s",
		(unsigned int)info->channel, (unsigned int)info->ap_count, strongest, others,
		total);
}


/**
 * @brief Log a scan's 5 GHz results on the console: each channel reached,
 *        and the best 80 MHz block with how busy it is ("nothing heard"
 *        rather than CHAN_RSSI_NONE's -127 dBm).
 *
 * @param s The scan.
 *
 * @sideeffects Writes log messages. errno unchanged.
 */
static void report_5g(const struct chan_stats *s)
{
	uint8_t best = recommend_5g(s);
	struct chan_info info;

	for (size_t i = 0; i < CHAN_5G_COUNT; i++) {
		chan_stats_channel_5g(s, chan_5g_channel(i), &info);
		report_channel(&info);
	}
	chan_stats_block_5g(s, best, &info);
	unsigned int last = chan_5g_block_last(best);

	if (info.interference == CHAN_RSSI_NONE) {
		LOG_INF("  5 GHz: use %u-%u (80 MHz): clear, nothing heard", (unsigned int)best,
			last);
	} else {
		LOG_INF("  5 GHz: use %u-%u (80 MHz): %s, interference %d dBm", (unsigned int)best,
			last, chan_level_name(chan_stats_level(info.interference)),
			info.interference);
	}
}


/**
 * @brief Log every network a scan kept, with the width it is counted at
 *        (chan_ap_width_mhz()), at debug level.
 *
 * SSIDs are printed through chan_ssid_printable(): they are whatever nearby access points
 * send, and control characters in them could rewrite the terminal. A hidden network is
 * shown by its BSSID, "hidden 02:1f:3c:12:34:56", as on the screen.
 *
 * @param s The scan.
 *
 * @sideeffects Writes debug log messages. errno may be set by snprintf() (only on an output
 *              error).
 */
static void report_networks(const struct chan_stats *s)
{
	char ssid[CHAN_SSID_MAX + 1];

	for (size_t i = 0; i < s->ap_count; i++) {
		if (chan_ap_hidden(&s->aps[i])) {
			chan_bssid_text(s->aps[i].bssid, HIDDEN_PREFIX, ssid, sizeof(ssid));
		} else {
			chan_ssid_printable(s->aps[i].ssid, ssid, sizeof(ssid));
		}
		LOG_DBG("  %-32s ch %3u %3d MHz %4d dBm", ssid, (unsigned int)s->aps[i].channel,
			chan_ap_width_mhz(&s->aps[i]), (int)s->aps[i].rssi);
	}
}


/**
 * @brief Log a scan's results on the console.
 *
 * @param s The scan.
 * @param scan_no Its number.
 *
 * @sideeffects Writes log messages. errno unchanged.
 */
static void report(const struct chan_stats *s, unsigned int scan_no)
{
	unsigned int n_2g4 = (unsigned int)chan_stats_band_count(s, CHAN_BAND_2G4);
	unsigned int other = (unsigned int)(s->ignored + s->dropped);

	if (IS_ENABLED(CONFIG_METER_BAND_5G)) {
		LOG_INF("Scan %u: %u networks on 2.4 GHz, %u on 5 GHz, %u other", scan_no, n_2g4,
			(unsigned int)chan_stats_band_count(s, CHAN_BAND_5G), other);
	} else {
		LOG_INF("Scan %u: %u networks on 2.4 GHz, %u other", scan_no, n_2g4, other);
	}
	for (uint8_t ch = CHAN_FIRST; ch <= CHAN_LAST; ch++) {
		struct chan_info info;

		chan_stats_channel(s, ch, &info);
		report_channel(&info);
	}
	if (IS_ENABLED(CONFIG_METER_BAND_5G)) {
		report_5g(s);
	}
	if (IS_ENABLED(CONFIG_METER_LOG_LEVEL_DBG)) {
		report_networks(s);
	}
}


/**
 * @brief Mark a scan as running, for the LED.
 *
 * @sideeffects Sets @c progress.busy, under @c lock. errno unchanged.
 */
static void set_busy(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	progress.busy = true;
	k_mutex_unlock(&lock);
}


/**
 * @brief Run one scan into the scan thread's buffer.
 *
 * @retval 0 Done; a scan that timed out counts as done with what it heard.
 * @retval <0 Negative errno: the scan failed, or couldn't start.
 *
 * @sideeffects Overwrites @c scanning with the scan's results and drives the radio
 *              (scanner_run()); logs a timeout or failure. errno unchanged.
 */
static int run_scan(void)
{
	int ret = scanner_run(&scanning, K_SECONDS(CONFIG_METER_SCAN_TIMEOUT_S));

	if (ret == -ETIMEDOUT) {
		LOG_WRN("Scan timed out: keeping what it heard");
		ret = 0;
	} else if (ret != 0) {
		LOG_WRN("Scan failed: %d", ret);
	}

	return ret;
}


/**
 * @brief Make a finished scan visible to the display thread.
 *
 * Forgets the access points that have faded out, adds what the scan
 * heard (if it didn't fail), and records the outcome.
 *
 * @param ret The scan's outcome (run_scan()).
 * @return The scan's number.
 *
 * @sideeffects Under @c lock: removes faded-out access points from @c history, merges @c
 *              scanning into it, and updates @c progress (scan count, last error, busy
 *              cleared). errno unchanged.
 */
static unsigned int publish(int ret)
{
	int64_t now = k_uptime_get();
	unsigned int scan_no;

	k_mutex_lock(&lock, K_FOREVER);
	ap_history_expire(&history, now, &fade);
	if (ret == 0) {
		ap_history_merge(&history, &scanning, now);
	}
	scan_no = ++progress.scans;
	progress.last_error = ret;
	progress.busy = false;
	k_mutex_unlock(&lock);

	return scan_no;
}


/**
 * @brief Restart the meter, to recover a Wi-Fi driver that has stopped
 *        working.
 *
 * A Wi-Fi driver can stop working for good: on the Pico W, the CYW43439 can
 * fail to wake from its deep sleep, after which every request to it times
 * out (CONFIG_METER_WIFI_KEEP_AWAKE keeps it out of that sleep). Only a
 * restart, which powers the chip off and on again, recovers it; it takes a
 * few seconds. The next start logs it (reset_note_restart()).
 *
 * @param ret The last scan's error.
 *
 * @sideeffects Logs the reason; those of reset_note_restart(): notes the error and the
 *              failures in a row, flushes the log, restarts the meter; does not return. errno
 *              unchanged.
 */
static FUNC_NORETURN void recover(int ret)
{
	LOG_ERR("%u scans in a row failed: restarting", health.failures);
	reset_note_restart(BOOT_WIFI_RESTART, ret, health.failures);
}


/**
 * @brief Run one scan, publish it and log it.
 *
 * @return True if it heard something; false if it failed or heard nothing.
 *
 * @sideeffects Those of set_busy(), run_scan(), publish() and report(): rewrites @c scanning,
 *              updates @c history and @c progress under @c lock, drives the radio, logs. Counts
 *              failures in a row in @c health; after CONFIG_METER_RECOVER_AFTER of them,
 *              restarts the meter (recover(ret)). errno unchanged.
 */
static bool scan_once(void)
{
	int ret;
	unsigned int scan_no;

	set_busy();
	ret = run_scan();
	scan_no = publish(ret);
	if (ret == 0) {
		report(&scanning, scan_no);
	}
	if (scan_health_record(&health, ret, CONFIG_METER_RECOVER_AFTER)) {
		recover(ret);
	}

	return ret == 0 && scanning.ap_count > 0U;
}


/**
 * @brief Scan for ever: every CONFIG_METER_SCAN_INTERVAL_S seconds, sooner
 *        after a scan that heard nothing, or at once on request.
 *
 * @param p1 Unused.
 * @param p2 Unused.
 * @param p3 Unused.
 *
 * @sideeffects Runs for ever on the scan thread: those of scan_once(), takes @c scan_now, and
 *              feeds the watchdog channel @c scan_watch each time round. errno unchanged.
 */
static void scan_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		watchdog_feed(scan_watch);

		bool heard = scan_once();

		(void)k_sem_take(&scan_now, K_SECONDS(heard ? CONFIG_METER_SCAN_INTERVAL_S
							    : CONFIG_METER_RETRY_S));
	}
}


/*
 * Public interface: documented in scan_task.h.
 *
 * The thread is created here rather than with K_THREAD_DEFINE, so that the thread object and
 * its stack can stay static.
 */
void scan_task_start(void)
{
	scan_watch = watchdog_add(BOOT_WATCHED_SCAN, SCAN_DEADLINE_MS);
	(void)k_thread_create(&scan_thread, scan_stack, K_THREAD_STACK_SIZEOF(scan_stack),
			      scan_loop, NULL, NULL, NULL, CONFIG_METER_SCAN_PRIORITY, 0,
			      K_NO_WAIT);
	k_thread_name_set(&scan_thread, "scan");
}


/*
 * Public interface: documented in scan_task.h.
 */
void scan_task_request(void)
{
	k_sem_give(&scan_now);
}


/*
 * Public interface: documented in scan_task.h.
 */
void scan_task_snapshot(int64_t now_ms, struct chan_stats *out, struct scan_task_state *state)
{
	k_mutex_lock(&lock, K_FOREVER);
	ap_history_snapshot(&history, now_ms, &fade, out);
	*state = progress;
	k_mutex_unlock(&lock);
}
