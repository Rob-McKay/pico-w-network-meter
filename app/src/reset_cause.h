/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The hardware's reset cause, as a start's record keeps it (enum
 * boot_hw_reset). Built with CONFIG_METER_RESET_CAUSE, which a board's way
 * of reading it selects: Zephyr's hwinfo API (CONFIG_METER_RESET_CAUSE_HWINFO,
 * reset_cause_hwinfo.c), or on the Pico W the RP2040's watchdog reason
 * register (CONFIG_METER_RESET_CAUSE_RP2040, target/pico/reset_cause_rp2040.c).
 * The mappings here are hardware independent, so they are unit tested.
 */

#ifndef METER_RESET_CAUSE_H_
#define METER_RESET_CAUSE_H_

#include <stdbool.h>
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
 * @brief Map the RP2040's watchdog reason register to a start record's
 *        reset cause.
 *
 * The register says only whether the watchdog caused the last reset: its
 * timer ran out (the whole system stopped), or software forced the reset
 * through it. Any other reset, power, the RUN pin, a debugger or a plain
 * software reset, leaves both bits clear, and those it can't tell apart.
 *
 * @param timer The register's TIMER bit.
 * @param force Its FORCE bit.
 * @return BOOT_HW_WATCHDOG for the timer, BOOT_HW_SOFTWARE for a forced
 *         reset, BOOT_HW_UNKNOWN for neither.
 *
 * @sideeffects None; errno unchanged.
 */
uint8_t reset_cause_from_watchdog(bool timer, bool force);

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
