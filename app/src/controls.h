/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Display Pack's four buttons (A, B, X, Y), through Zephyr's input
 * subsystem (gpio-keys in the board overlay).
 */

#ifndef METER_CONTROLS_H_
#define METER_CONTROLS_H_

#include <zephyr/kernel.h>

/**
 * @brief Start queueing button presses.
 *
 * Call once, before controls_wait(); presses before it are ignored.
 */
void controls_init(void);

/**
 * @brief Wait for a button press.
 *
 * Presses are queued (a few) between calls; releases are ignored.
 *
 * @param timeout Longest wait.
 * @return The button's key code (INPUT_KEY_A, INPUT_KEY_B, INPUT_KEY_X or
 *         INPUT_KEY_Y), or -EAGAIN if none was pressed in time.
 */
int controls_wait(k_timeout_t timeout);

#endif /* METER_CONTROLS_H_ */
