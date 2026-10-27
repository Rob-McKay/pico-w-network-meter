/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Turns key presses and releases into the presses the meter acts on,
 * including X and Y pressed together. Hardware independent, so it is unit
 * tested on native_sim.
 */

#ifndef METER_KEYS_H_
#define METER_KEYS_H_

#include <stdbool.h>
#include <stdint.h>

/** X and Y pressed together: a code no key has. */
#define KEYS_X_AND_Y 0x1000

/** Which of X and Y are held down. */
struct keys_state {
	bool x; /**< X is down */
	bool y; /**< Y is down */
};

/**
 * @brief Take one key event.
 *
 * A press of X while Y is down, or of Y while X is down, gives
 * KEYS_X_AND_Y instead of the key; the first key's own press has already
 * been given.
 *
 * @param[in,out] s Which keys are down.
 * @param code The key (INPUT_KEY_*).
 * @param pressed Pressed if true, released if false.
 * @return The press to act on (the key code, or KEYS_X_AND_Y), or -1 for a
 *         release.
 *
 * @sideeffects None (writes only through @p s); errno unchanged.
 */
int keys_event(struct keys_state *s, uint16_t code, bool pressed);

#endif /* METER_KEYS_H_ */
