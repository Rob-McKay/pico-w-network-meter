/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Pico W's reset cause, from the RP2040's watchdog reason register
 * (CONFIG_METER_RESET_CAUSE_RP2040). Zephyr's hwinfo driver doesn't read it
 * on the RP2040, and its chip reset register keeps the last power-on until
 * the next one, so the watchdog register is the one reliable cause: it says
 * whether the hardware watchdog ended the run before.
 */

#include <stdbool.h>
#include <stdint.h>

#include <hardware/regs/watchdog.h>
#include <hardware/structs/watchdog.h>

#include "reset_cause.h"

/*
 * Public interface: documented in reset_cause.h.
 *
 * The register is read-only and set afresh by every reset, so there is nothing to clear.
 */
uint8_t reset_cause_take(void)
{
	uint32_t reason = watchdog_hw->reason;

	return reset_cause_from_watchdog((reason & WATCHDOG_REASON_TIMER_BITS) != 0U,
					 (reason & WATCHDOG_REASON_FORCE_BITS) != 0U);
}
