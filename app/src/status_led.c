/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/util.h>

#include "status_led.h"

#define LEDS_NODE DT_NODELABEL(pack_leds)

/* Index of each LED in the gpio-leds node: its child order. */
#define LED_RED DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_red))
#define LED_GREEN DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_green))
#define LED_BLUE DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_blue))
#define LED_BACKLIGHT DT_NODE_CHILD_IDX(DT_NODELABEL(pack_backlight))

static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);

/* Red, green and blue for each status. */
static const bool rgb_of[][3] = {
	[STATUS_OFF] = {false, false, false},
	[STATUS_SCANNING] = {false, false, true},
	[STATUS_CLEAR] = {false, true, false},
	[STATUS_MODERATE] = {true, true, false},
	[STATUS_BUSY] = {true, false, false},
	[STATUS_ERROR] = {true, false, true},
};

/**
 * @brief Turn one LED on or off.
 *
 * @param led Index in the gpio-leds node.
 * @param on On.
 */
static void led_set(uint32_t led, bool on)
{
	(void)(on ? led_on(leds, led) : led_off(leds, led));
}

int status_init(void)
{
	if (!device_is_ready(leds)) {
		return -ENODEV;
	}
	led_set(LED_BACKLIGHT, true);
	status_set(STATUS_OFF);

	return 0;
}

void status_set(enum status s)
{
	if ((size_t)s >= ARRAY_SIZE(rgb_of) || !device_is_ready(leds)) {
		return;
	}
	led_set(LED_RED, rgb_of[s][0]);
	led_set(LED_GREEN, rgb_of[s][1]);
	led_set(LED_BLUE, rgb_of[s][2]);
}
