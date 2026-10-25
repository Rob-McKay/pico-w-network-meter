/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The Display Pack's RGB LED, and its backlight, through Zephyr's LED API
 * (gpio-leds in the board overlay).
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
 */
int status_init(void);

/**
 * @brief Show a status on the RGB LED.
 *
 * @param s Status.
 */
void status_set(enum status s);

#endif /* METER_STATUS_LED_H_ */
