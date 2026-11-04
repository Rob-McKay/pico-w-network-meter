/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/input/input-event-codes.h>

#include "keys.h"

/*
 * Public interface: documented in keys.h.
 */
int keys_event(struct keys_state *s, uint16_t code, bool pressed)
{
	bool chord = false;

	if (code == INPUT_KEY_X) {
		s->x = pressed;
		chord = pressed && s->y;
	} else if (code == INPUT_KEY_Y) {
		s->y = pressed;
		chord = pressed && s->x;
	}
	if (!pressed) {
		return -1;
	}

	return chord ? KEYS_X_AND_Y : (int)code;
}
