/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Display Pack's RGB LED, dimmed by PWM (pwm-leds in the board
 * overlay), and its backlight (gpio-leds), through Zephyr's LED API.
 */

#ifndef METER_STATUS_LED_H_
#define METER_STATUS_LED_H_

/** What the RGB LED shows. */
enum status {
	STATUS_OFF,
	STATUS_SCANNING, /**< blue */
	STATUS_CLEAR,    /**< green: the recommended channel is quiet */
	STATUS_MODERATE, /**< amber */
	STATUS_BUSY,     /**< red: even the best channel is busy */
	STATUS_ERROR,    /**< magenta */
};

/**
 * @brief Turn the backlight on and the RGB LED off.
 *
 * @retval 0 Done.
 * @retval -ENODEV The LED controller is not ready.
 *
 * @sideeffects Turns the backlight on and the RGB LED off. errno unchanged.
 */
int status_init(void);

/**
 * @brief Show a status on the RGB LED.
 *
 * Does nothing if it already shows @p s, so it can be called often.
 *
 * @param s Status.
 *
 * @sideeffects Drives the RGB LED's PWM channels and caches @p s (main thread only). errno
 *              unchanged.
 */
void status_set(enum status s);

#endif /* METER_STATUS_LED_H_ */
