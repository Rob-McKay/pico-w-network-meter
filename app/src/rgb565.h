/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * RGB565 pixels as the TFT takes them over SPI: high byte first, Zephyr's
 * PIXEL_FORMAT_RGB_565X.
 */

#ifndef METER_RGB565_H_
#define METER_RGB565_H_

#include <stdint.h>

#include <zephyr/sys/byteorder.h>

/**
 * @brief Make an RGB565 pixel, stored high byte first.
 *
 * @param r Red, 0-255.
 * @param g Green, 0-255.
 * @param b Blue, 0-255.
 * @return The pixel.
 *
 * @sideeffects None; errno unchanged.
 */
static inline uint16_t rgb565x(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t px = (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | ((unsigned int)b >> 3));

	return sys_cpu_to_be16(px);
}

/**
 * @brief Darken an RGB565 pixel (high byte first) towards black.
 *
 * @param px The pixel, as rgb565x() makes it.
 * @param level 255 leaves it as it is, 0 makes it black.
 * @return The darkened pixel.
 *
 * @sideeffects None; errno unchanged.
 */
static inline uint16_t rgb565x_fade(uint16_t px, uint8_t level)
{
	unsigned int v = sys_be16_to_cpu(px);
	unsigned int r = ((v >> 11U) & 0x1fU) * level / 255U;
	unsigned int g = ((v >> 5U) & 0x3fU) * level / 255U;
	unsigned int b = (v & 0x1fU) * level / 255U;

	return sys_cpu_to_be16((uint16_t)((r << 11U) | (g << 5U) | b));
}

#endif /* METER_RGB565_H_ */
