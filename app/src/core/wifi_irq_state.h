/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Debugging Wi-Fi failures: what the CYW43439's interrupt line says,
 * from samples of it. Hardware independent, so it is unit tested on
 * native_sim; src/target/pico/wifi_debug.c takes the samples.
 */

#ifndef METER_WIFI_IRQ_STATE_H_
#define METER_WIFI_IRQ_STATE_H_

#include <stdbool.h>
#include <stddef.h>

/** What the chip's interrupt line was doing while it was sampled. */
enum wifi_irq_state {
	WIFI_IRQ_IDLE, /**< never asserted: the chip has nothing for the Pico */
	WIFI_IRQ_BUSY, /**< changing: a transfer, or the driver at work */
	WIFI_IRQ_HELD, /**< asserted throughout: a reply waits to be read */
	WIFI_IRQ_STATE_COUNT,
};

/**
 * @brief Tell what the interrupt line was doing from samples of it.
 *
 * @param asserted The samples, in order: true where the line was asserted.
 * @param count How many there are; none counts as idle.
 * @return Its state.
 *
 * @sideeffects None; errno unchanged.
 */
enum wifi_irq_state wifi_irq_classify(const bool *asserted, size_t count);

/**
 * @brief Name a state, for the log.
 *
 * @param state The state.
 * @return Its name; "unknown" for a value that isn't one.
 *
 * @sideeffects None; errno unchanged.
 */
const char *wifi_irq_state_name(enum wifi_irq_state state);

#endif /* METER_WIFI_IRQ_STATE_H_ */
