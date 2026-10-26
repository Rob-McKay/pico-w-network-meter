/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#include "ap_history.h"
#include "scan_health.h"
#include "scan_task.h"
#include "scanner.h"

LOG_MODULE_REGISTER(scan_task, CONFIG_METER_LOG_LEVEL);

BUILD_ASSERT(CONFIG_METER_FADE_END_S > CONFIG_METER_FADE_START_S,
	     "CONFIG_METER_FADE_END_S must be more than CONFIG_METER_FADE_START_S");

static const struct ap_fade fade = {
	.start_ms = (int64_t)CONFIG_METER_FADE_START_S * MSEC_PER_SEC,
	.end_ms = (int64_t)CONFIG_METER_FADE_END_S * MSEC_PER_SEC,
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

/* The scan in progress, and the failures in a row: only the scan thread touches them. */
static struct chan_stats scanning;
static struct scan_health health;

/* Shared with the display thread, under lock. */
static K_MUTEX_DEFINE(lock);
static struct ap_history history;
static struct scan_task_state progress;

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
	LOG_INF("Scan %u: %u networks on 2.4 GHz, %u other", scan_no, (unsigned int)s->ap_count,
		(unsigned int)(s->ignored + s->dropped));
	for (uint8_t ch = CHAN_FIRST; ch <= CHAN_LAST; ch++) {
		struct chan_info info;

		chan_stats_channel(s, ch, &info);
		if (info.ap_count > 0U || info.interference != CHAN_RSSI_NONE) {
			LOG_INF("  ch %2u: %u networks, strongest %4d dBm, from other channels %4d "
				"dBm, total %4d dBm",
				(unsigned int)ch, (unsigned int)info.ap_count, info.best_rssi,
				info.adjacent, info.interference);
		}
	}
	for (size_t i = 0; i < s->ap_count; i++) {
		LOG_DBG("  %-32s ch %2u %4d dBm", s->aps[i].ssid, (unsigned int)s->aps[i].channel,
			s->aps[i].rssi);
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
 * The AIROC driver can run out of packet buffers for good (a leak in it or
 * its WHD library), after which every scan fails to start. Only a restart
 * gets them back; it takes a few seconds.
 *
 * @sideeffects Logs the reason, flushes the log, and restarts the Pico (sys_reboot()); does
 *              not return. errno unchanged.
 */
static FUNC_NORETURN void recover(void)
{
	LOG_ERR("%u scans in a row failed: restarting", health.failures);
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

/**
 * @brief Run one scan, publish it and log it.
 *
 * @return True if it heard something; false if it failed or heard nothing.
 *
 * @sideeffects Those of set_busy(), run_scan(), publish() and report(): rewrites @c scanning,
 *              updates @c history and @c progress under @c lock, drives the radio, logs. Counts
 *              failures in a row in @c health; after CONFIG_METER_RECOVER_AFTER of them,
 *              restarts the Pico (recover()). errno unchanged.
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
		recover();
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
 * @sideeffects Runs for ever on the scan thread: those of scan_once(), and takes @c scan_now.
 *              errno unchanged.
 */
static void scan_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
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
