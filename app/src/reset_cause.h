/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The hardware's reset cause, through Zephyr's hwinfo API, as a start's
 * record keeps it (enum boot_hw_reset). Built with
 * CONFIG_METER_RESET_CAUSE_HWINFO, for boards whose hwinfo driver reports
 * the cause reliably.
 */

#ifndef METER_RESET_CAUSE_H_
#define METER_RESET_CAUSE_H_

#include <stdint.h>

#include "boot_record.h"

/**
 * @brief Map hwinfo's reset cause flags (RESET_PIN, RESET_WATCHDOG, ...) to
 *        a start record's reset cause.
 *
 * Several flags can be set at once (a brown-out often shows as a power-on
 * too), so the most telling wins: brown-out, watchdog, CPU fault, reset
 * pin, software, power-on, wake-up; any other flag is BOOT_HW_OTHER, none
 * at all BOOT_HW_UNKNOWN.
 *
 * @param flags hwinfo_get_reset_cause()'s flags.
 * @return enum boot_hw_reset.
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t reset_cause_from_flags(uint32_t flags);

/**
 * @brief Read the hardware's reset cause for this start, and clear it for
 *        the next.
 *
 * Call once, at start-up.
 *
 * @return enum boot_hw_reset; BOOT_HW_UNKNOWN if the driver can't say.
 *
 * @sideeffects Reads the reset cause from the hardware and clears it (a driver that can't
 *              clear it is left as it is). errno unchanged.
 */
uint8_t reset_cause_take(void);

#endif /* METER_RESET_CAUSE_H_ */
