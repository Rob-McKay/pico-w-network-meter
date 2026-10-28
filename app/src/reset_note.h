/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The note a restart leaves for the next start (struct boot_note), in RAM
 * that start-up doesn't clear: a restart keeps it, a power cut loses it.
 * Every restart the meter makes goes through reset_note_restart(), which
 * writes why; so do fatal errors, through Zephyr's fatal error handler,
 * which this module replaces.
 */

#ifndef METER_RESET_NOTE_H_
#define METER_RESET_NOTE_H_

#include <stdint.h>

#include <zephyr/toolchain.h>

#include "boot_record.h"

/**
 * @brief Take the note the run before left, and start this run's.
 *
 * Call once, first thing at start-up.
 *
 * @param[out] out The note found: not valid (boot_note_valid()) after a
 *             power cut.
 *
 * @sideeffects Reads the note in RAM, then rewrites it as running (cause BOOT_NONE, uptime 0).
 *              errno unchanged.
 */
void reset_note_take(struct boot_note *out);

/**
 * @brief Record how long this run has lasted, in case it ends without
 *        warning.
 *
 * @param uptime_ms k_uptime_get().
 *
 * A note that already holds a restart's cause is left alone.
 *
 * @sideeffects Rewrites the note's uptime in RAM, interrupts locked. errno unchanged.
 */
void reset_note_alive(int64_t uptime_ms);

/**
 * @brief Restart the meter, leaving a note of why.
 *
 * Safe from any thread or interrupt.
 *
 * @param cause Why: BOOT_WIFI_RESTART, BOOT_CRASH or BOOT_WATCHDOG.
 * @param detail See struct boot_record.
 * @param arg See struct boot_record.
 *
 * @sideeffects Rewrites the note in RAM, logs why and flushes the log, stops the hardware
 *              watchdog (watchdog_stop()), and restarts the Pico (sys_reboot()); does not
 *              return. errno unchanged.
 */
FUNC_NORETURN void reset_note_restart(enum boot_cause cause, int32_t detail, uint32_t arg);

#endif /* METER_RESET_NOTE_H_ */
