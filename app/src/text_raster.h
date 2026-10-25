/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Draws a line of text into RGB565 pixels for a colour display, with the
 * character framebuffer's bitmap fonts (struct cfb_font). No I/O here.
 */

#ifndef METER_TEXT_RASTER_H_
#define METER_TEXT_RASTER_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/display/cfb.h>

/**
 * @brief Make an RGB565 pixel, stored high byte first: Zephyr's
 *        PIXEL_FORMAT_RGB_565X, as the panel takes it over SPI.
 *
 * @param r Red, 0-255.
 * @param g Green, 0-255.
 * @param b Blue, 0-255.
 * @return The pixel.
 *
 * @sideeffects None; errno unchanged.
 */
uint16_t raster_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Draw a line of text across a band of pixels.
 *
 * The band is @p width pixels wide and the font's height tall, row by
 * row. Text past the band's right edge is cut; the rest of the band is
 * background. Printable ASCII is drawn from the font; any other byte
 * shows as '?', except UTF-8 continuation bytes, which take no space.
 *
 * @param font Font (vertically packed, as the CFB fonts are).
 * @param text Line of text.
 * @param[out] band Pixels: @p width x the font's height.
 * @param width Band width in pixels.
 * @param fg Text colour (raster_rgb()).
 * @param bg Background colour (raster_rgb()).
 *
 * @sideeffects None (writes only through @p band); errno unchanged.
 */
void raster_text(const struct cfb_font *font, const char *text, uint16_t *band, size_t width,
		 uint16_t fg, uint16_t bg);

#endif /* METER_TEXT_RASTER_H_ */
