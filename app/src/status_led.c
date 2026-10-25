/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/util.h>

#include "status_led.h"

/* The RGB LED (pwm-leds, dimmable) and the backlight (gpio-leds). */
#define RGB_NODE  DT_NODELABEL(pack_rgb_led)
#define LEDS_NODE DT_NODELABEL(pack_leds)

/* Index of each LED in its node: its child order. */
#define LED_RED       DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_red))
#define LED_GREEN     DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_green))
#define LED_BLUE      DT_NODE_CHILD_IDX(DT_NODELABEL(pack_led_blue))
#define LED_BACKLIGHT DT_NODE_CHILD_IDX(DT_NODELABEL(pack_backlight))

static const struct device *const rgb = DEVICE_DT_GET(RGB_NODE);
static const struct device *const leds = DEVICE_DT_GET(LEDS_NODE);

/* Red, green and blue for each status. */
static const bool rgb_of[][3] = {
	[STATUS_OFF] = {false, false, false},  [STATUS_SCANNING] = {false, false, true},
	[STATUS_CLEAR] = {false, true, false}, [STATUS_MODERATE] = {true, true, false},
	[STATUS_BUSY] = {true, false, false},  [STATUS_ERROR] = {true, false, true},
};

/**
 * @brief Turn one colour of the RGB LED on, at CONFIG_METER_LED_BRIGHTNESS,
 *        or off.
 *
 * @param led Index in the pwm-leds node.
 * @param on On.
 *
 * @sideeffects Sets one PWM channel of the RGB LED. errno unchanged.
 */
static void rgb_set(uint32_t led, bool on)
{
	(void)led_set_brightness(rgb, led, on ? CONFIG_METER_LED_BRIGHTNESS : 0U);
}

/*
 * Public interface: documented in status_led.h.
 */
int status_init(void)
{
	if (!device_is_ready(leds) || !device_is_ready(rgb)) {
		return -ENODEV;
	}
	(void)led_on(leds, LED_BACKLIGHT);
	status_set(STATUS_OFF);

	return 0;
}

/*
 * Public interface: documented in status_led.h.
 *
 * Caches the status shown (main thread only), so that calls repeated every redraw don't rewrite
 * the PWM channels.
 */
void status_set(enum status s)
{
	static enum status shown = STATUS_OFF;
	static bool set;

	if ((size_t)s >= ARRAY_SIZE(rgb_of) || !device_is_ready(rgb) || (set && s == shown)) {
		return;
	}
	shown = s;
	set = true;
	rgb_set(LED_RED, rgb_of[s][0]);
	rgb_set(LED_GREEN, rgb_of[s][1]);
	rgb_set(LED_BLUE, rgb_of[s][2]);
}
