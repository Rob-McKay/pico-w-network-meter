/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/task_wdt/task_wdt.h>

#include "reset_note.h"
#include "watchdog.h"

LOG_MODULE_REGISTER(watchdog, CONFIG_METER_LOG_LEVEL);

static const struct device *const hw_wdt = DEVICE_DT_GET(DT_NODELABEL(wdt0));

/* The threads, for the task watchdog's user data: a pointer to one tells
 * on_missed() which thread missed its deadline. Never written.
 */
static enum boot_watched watched[] = {BOOT_WATCHED_MAIN, BOOT_WATCHED_SCAN};

/**
 * @brief Restart because a thread missed its deadline (task watchdog
 *        callback, from its timer's interrupt).
 *
 * @param channel The channel.
 * @param user_data The thread: an entry of @c watched.
 *
 * @sideeffects Those of reset_note_restart(): notes which thread, restarts; does not return.
 *              errno unchanged.
 */
static void on_missed(int channel, void *user_data)
{
	const enum boot_watched *who = user_data;

	ARG_UNUSED(channel);
	reset_note_restart(BOOT_WATCHDOG, (int32_t)*who, 0);
}

/*
 * Public interface: documented in watchdog.h.
 */
int watchdog_init(void)
{
	int ret = task_wdt_init(device_is_ready(hw_wdt) ? hw_wdt : NULL);

	if (ret != 0) {
		LOG_ERR("Watchdog not started: %d", ret);
	}

	return ret;
}

/*
 * Public interface: documented in watchdog.h.
 */
int watchdog_add(enum boot_watched who, uint32_t timeout_ms)
{
	int ret;

	if ((size_t)who >= ARRAY_SIZE(watched)) {
		return -EINVAL;
	}
	ret = task_wdt_add(timeout_ms, on_missed, &watched[who]);
	if (ret < 0) {
		LOG_ERR("Can't watch thread %d: %d", (int)who, ret);
	}

	return ret;
}

/*
 * Public interface: documented in watchdog.h.
 */
void watchdog_feed(int channel)
{
	if (channel >= 0) {
		(void)task_wdt_feed(channel);
	}
}

/*
 * Public interface: documented in watchdog.h.
 *
 * wdt_disable() fails harmlessly (-EFAULT) if the watchdog never started.
 */
void watchdog_stop(void)
{
	if (device_is_ready(hw_wdt)) {
		(void)wdt_disable(hw_wdt);
	}
}
