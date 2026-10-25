/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "controls.h"

/* Presses not yet taken by controls_wait(). */
#define QUEUED_PRESSES 4

/* Set up by controls_init(); K_MSGQ_DEFINE cannot make the queue static. */
static uint16_t press_buf[QUEUED_PRESSES];
static struct k_msgq presses;
static atomic_t ready;

/**
 * @brief Queue a button press (input thread).
 *
 * @param evt Input event.
 * @param user_data Unused.
 *
 * @sideeffects Puts the press on the @c presses queue (input thread); a press is dropped if the
 *              queue is full. errno unchanged.
 */
static void on_input(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (atomic_get(&ready) != 0 && evt->type == INPUT_EV_KEY && evt->value != 0) {
		/* A full queue drops the press: the user pressed faster than
		 * the screen could follow.
		 */
		(void)k_msgq_put(&presses, &evt->code, K_NO_WAIT);
	}
}

INPUT_CALLBACK_DEFINE(NULL, on_input, NULL);

/*
 * Public interface: documented in controls.h.
 *
 * The ready flag is set after the queue is initialised. Zephyr's atomics are sequentially
 * consistent, so on_input() never sees the flag set before the queue exists.
 */
void controls_init(void)
{
	k_msgq_init(&presses, (char *)press_buf, sizeof(press_buf[0]), QUEUED_PRESSES);
	atomic_set(&ready, 1);
}

/*
 * Public interface: documented in controls.h.
 */
int controls_wait(k_timeout_t timeout)
{
	uint16_t code;

	if (k_msgq_get(&presses, &code, timeout) != 0) {
		return -EAGAIN;
	}

	return code;
}
