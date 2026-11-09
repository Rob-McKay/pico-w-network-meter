/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/util.h>

#include "reset_cause.h"
#include "boot_record.h"

/* Flags for a reset the CPU caused itself by faulting. */
#define CPU_FAULT_FLAGS (RESET_CPU_LOCKUP | RESET_PARITY | RESET_SECURITY)

/*
 * Public interface: documented in reset_cause.h.
 */
uint8_t reset_cause_from_flags(uint32_t flags)
{
	static const struct {
		uint32_t flags;
		uint8_t cause;
	} order[] = {
		{RESET_BROWNOUT, BOOT_HW_BROWNOUT},   {RESET_WATCHDOG, BOOT_HW_WATCHDOG},
		{CPU_FAULT_FLAGS, BOOT_HW_CPU_FAULT}, {RESET_PIN, BOOT_HW_PIN},
		{RESET_SOFTWARE, BOOT_HW_SOFTWARE},   {RESET_POR, BOOT_HW_POWER_ON},
		{RESET_LOW_POWER_WAKE, BOOT_HW_WAKE},
	};

	for (size_t i = 0; i < ARRAY_SIZE(order); i++) {
		if ((flags & order[i].flags) != 0U) {
			return order[i].cause;
		}
	}

	return flags != 0U ? (uint8_t)BOOT_HW_OTHER : (uint8_t)BOOT_HW_UNKNOWN;
}


/*
 * Public interface: documented in reset_cause.h.
 *
 * The timer wins if both are set: the system stopped, whatever else happened.
 */
uint8_t reset_cause_from_watchdog(bool timer, bool force)
{
	if (timer) {
		return BOOT_HW_WATCHDOG;
	}

	return force ? (uint8_t)BOOT_HW_SOFTWARE : (uint8_t)BOOT_HW_UNKNOWN;
}
