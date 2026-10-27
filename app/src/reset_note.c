/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/section_tags.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

#include "reset_note.h"
#include "watchdog.h"

LOG_MODULE_REGISTER(reset_note, CONFIG_METER_LOG_LEVEL);

/* Not cleared at start-up: holds the run before's note until
 * reset_note_take(). Written with interrupts locked, as a restart can come
 * from any thread or interrupt.
 */
static __noinit struct boot_note note;

/**
 * @brief Convert an uptime to whole seconds that fit the note.
 *
 * @param uptime_ms Milliseconds.
 * @return Seconds, 0 to UINT32_MAX.
 *
 * @sideeffects None; errno unchanged.
 */
static uint32_t seconds(int64_t uptime_ms)
{
	return (uint32_t)CLAMP(uptime_ms / MSEC_PER_SEC, 0, (int64_t)UINT32_MAX);
}

/**
 * @brief Rewrite the note.
 *
 * @param cause Why the run is ending, or BOOT_NONE while it runs.
 * @param detail See struct boot_record.
 * @param arg See struct boot_record.
 * @param uptime_ms How long the run has lasted.
 *
 * @sideeffects Rewrites @c note, interrupts locked. errno unchanged.
 */
static void write_note(enum boot_cause cause, int32_t detail, uint32_t arg, int64_t uptime_ms)
{
	unsigned int key = irq_lock();

	note.cause = (uint8_t)cause;
	note.detail = detail;
	note.arg = arg;
	note.uptime_s = seconds(uptime_ms);
	boot_note_seal(&note);
	irq_unlock(key);
}

/*
 * Public interface: documented in reset_note.h.
 */
void reset_note_take(struct boot_note *out)
{
	*out = note;
	write_note(BOOT_NONE, 0, 0, 0);
}

/*
 * Public interface: documented in reset_note.h.
 *
 * Leaves a note that already has a cause alone: another thread may be part-way through
 * reset_note_restart(), flushing the log before it restarts.
 */
void reset_note_alive(int64_t uptime_ms)
{
	unsigned int key = irq_lock();

	if (note.cause == BOOT_NONE) {
		note.uptime_s = seconds(uptime_ms);
		boot_note_seal(&note);
	}
	irq_unlock(key);
}

/*
 * Public interface: documented in reset_note.h.
 */
FUNC_NORETURN void reset_note_restart(enum boot_cause cause, int32_t detail, uint32_t arg)
{
	write_note(cause, detail, arg, k_uptime_get());
	LOG_ERR("Restarting: cause %d, detail %d, arg 0x%08x", (int)cause, (int)detail,
		(unsigned int)arg);
	LOG_PANIC();
	watchdog_stop();
	sys_reboot(SYS_REBOOT_COLD);
}

/**
 * @brief Restart after a fatal error, noting it, instead of halting
 *        (replaces Zephyr's handler).
 *
 * Zephyr has already logged the error and the registers.
 *
 * @param reason The error (K_ERR_*).
 * @param esf The exception's saved registers, or NULL.
 *
 * @sideeffects Those of reset_note_restart(): notes the crash with the program counter, flushes
 *              the log, restarts; does not return. errno unchanged.
 */
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	uint32_t pc = 0;

#ifdef CONFIG_CPU_CORTEX_M
	if (esf != NULL) {
		pc = esf->basic.pc;
	}
#else
	ARG_UNUSED(esf);
#endif
	reset_note_restart(BOOT_CRASH, (int32_t)MIN(reason, (unsigned int)INT32_MAX), pc);
}
