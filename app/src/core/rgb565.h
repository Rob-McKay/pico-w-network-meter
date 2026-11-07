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

/* An RGB565 pixel's fields, from the top bit: red, green, blue. */
#define RGB565_R_BITS  5U
#define RGB565_G_BITS  6U
#define RGB565_B_BITS  5U
#define RGB565_G_SHIFT RGB565_B_BITS
#define RGB565_R_SHIFT (RGB565_G_BITS + RGB565_B_BITS)
#define RGB565_R_MAX   ((1U << RGB565_R_BITS) - 1U)
#define RGB565_G_MAX   ((1U << RGB565_G_BITS) - 1U)
#define RGB565_B_MAX   ((1U << RGB565_B_BITS) - 1U)
/* The 8-bit colour channels rgb565x() takes, and the fade levels: 0-255. */
#define RGB8_BITS      8U
#define RGB8_MAX       255U

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
	unsigned int r5 = (unsigned int)r >> (RGB8_BITS - RGB565_R_BITS);
	unsigned int g6 = (unsigned int)g >> (RGB8_BITS - RGB565_G_BITS);
	unsigned int b5 = (unsigned int)b >> (RGB8_BITS - RGB565_B_BITS);
	uint16_t px = (uint16_t)((r5 << RGB565_R_SHIFT) | (g6 << RGB565_G_SHIFT) | b5);

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
	unsigned int r = ((v >> RGB565_R_SHIFT) & RGB565_R_MAX) * level / RGB8_MAX;
	unsigned int g = ((v >> RGB565_G_SHIFT) & RGB565_G_MAX) * level / RGB8_MAX;
	unsigned int b = (v & RGB565_B_MAX) * level / RGB8_MAX;

	return sys_cpu_to_be16((uint16_t)((r << RGB565_R_SHIFT) | (g << RGB565_G_SHIFT) | b));
}

#endif /* METER_RGB565_H_ */
