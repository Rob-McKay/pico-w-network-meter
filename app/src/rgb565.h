/*
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
 */
static inline uint16_t rgb565x(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t px = (uint16_t)(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | ((unsigned int)b >> 3));

	return sys_cpu_to_be16(px);
}

#endif /* METER_RGB565_H_ */
