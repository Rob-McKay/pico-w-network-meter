/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The reset cause through Zephyr's hwinfo API (CONFIG_METER_RESET_CAUSE_HWINFO).
 */

#include <stdint.h>

#include <zephyr/drivers/hwinfo.h>

#include "boot_record.h"
#include "reset_cause.h"

/*
 * Public interface: documented in reset_cause.h.
 *
 * Cleared after reading, as Zephyr asks: some drivers accumulate flags across resets until they
 * are cleared. A driver without clearing returns -ENOSYS, which changes nothing here.
 */
uint8_t reset_cause_take(void)
{
	uint32_t flags = 0;

	if (hwinfo_get_reset_cause(&flags) != 0) {
		return BOOT_HW_UNKNOWN;
	}
	(void)hwinfo_clear_reset_cause();

	return reset_cause_from_flags(flags);
}
