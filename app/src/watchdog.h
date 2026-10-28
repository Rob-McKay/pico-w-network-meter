/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Restarts the meter if a thread stops responding: Zephyr's task
 * watchdog, one channel a thread, with the RP2040's hardware watchdog
 * behind it in case the kernel itself stops. A thread that misses its
 * deadline restarts the meter through reset_note_restart(), noting which
 * thread it was.
 */

#ifndef METER_WATCHDOG_H_
#define METER_WATCHDOG_H_

#include <stdint.h>

#include "boot_record.h"

/**
 * @brief Start the watchdog.
 *
 * @retval 0 Started, with the hardware watchdog behind it.
 * @retval <0 Negative errno from task_wdt_init(); the meter runs unwatched.
 *
 * @sideeffects Starts the task watchdog's timer and the hardware watchdog; logs a failure.
 *              errno unchanged.
 */
int watchdog_init(void);

/**
 * @brief Watch a thread: it must call watchdog_feed() at least every
 *        @p timeout_ms.
 *
 * @param who The thread, noted if it misses its deadline.
 * @param timeout_ms Its deadline.
 * @return The channel to feed, or a negative errno: -EINVAL for an unknown
 *         thread, or from task_wdt_add(). The thread isn't watched then, and
 *         watchdog_feed() does nothing with it.
 *
 * @sideeffects Adds a task watchdog channel; logs a failure. errno unchanged.
 */
int watchdog_add(enum boot_watched who, uint32_t timeout_ms);

/**
 * @brief Tell the watchdog a thread is still working.
 *
 * @param channel From watchdog_add(); a negative one is ignored.
 *
 * @sideeffects Restarts the channel's deadline. errno unchanged.
 */
void watchdog_feed(int channel);

/**
 * @brief Stop the hardware watchdog, before a restart.
 *
 * The RP2040's watchdog driver doesn't stop the watchdog at start-up, so a
 * restart that left it running could have it fire during the next
 * start-up, before anything feeds it. Safe from any thread or interrupt.
 *
 * @sideeffects Disables the hardware watchdog if it was started. errno unchanged.
 */
void watchdog_stop(void);

#endif /* METER_WATCHDOG_H_ */
