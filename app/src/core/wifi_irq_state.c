/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stddef.h>

#include "wifi_irq_state.h"

/*
 * Public interface: documented in wifi_irq_state.h.
 */
enum wifi_irq_state wifi_irq_classify(const bool *asserted, size_t count)
{
	size_t held = 0;

	if (asserted == NULL || count == 0U) {
		return WIFI_IRQ_IDLE;
	}
	for (size_t i = 0; i < count; i++) {
		if (asserted[i]) {
			held++;
		}
	}
	if (held == 0U) {
		return WIFI_IRQ_IDLE;
	}

	return held == count ? WIFI_IRQ_HELD : WIFI_IRQ_BUSY;
}

/*
 * Public interface: documented in wifi_irq_state.h.
 */
const char *wifi_irq_state_name(enum wifi_irq_state state)
{
	switch (state) {
	case WIFI_IRQ_IDLE:
		return "idle";
	case WIFI_IRQ_BUSY:
		return "busy";
	case WIFI_IRQ_HELD:
		return "held asserted";
	default:
		return "unknown";
	}
}
