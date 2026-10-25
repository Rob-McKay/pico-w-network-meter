/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drawing on the Display Pack's ST7789V panel (zephyr,display, RGB565 high
 * byte first) without a frame buffer: rectangles and lines of text are
 * sent as they are drawn, through one small band buffer.
 */

#ifndef METER_GFX_H_
#define METER_GFX_H_

#include <stdint.h>

#include <zephyr/display/cfb.h>

/**
 * @brief Make an RGB565 colour for the display.
 *
 * @param r Red, 0-255.
 * @param g Green, 0-255.
 * @param b Blue, 0-255.
 * @return The colour.
 */
uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Start the display: clear it to @p bg and turn it on.
 *
 * @param bg Background colour (gfx_rgb()).
 * @retval 0 Ready.
 * @retval -ENODEV The display is not ready.
 * @retval -ENOTSUP The display is not RGB565 or is wider than supported.
 */
int gfx_init(uint16_t bg);

/**
 * @brief Display width.
 *
 * @return Pixels.
 */
int gfx_width(void);

/**
 * @brief Display height.
 *
 * @return Pixels.
 */
int gfx_height(void);

/**
 * @brief Find a font by height.
 *
 * @param height Pixels (16 or 24 with the default fonts).
 * @return The font, or the smallest font if none has that height.
 */
const struct cfb_font *gfx_font(int height);

/**
 * @brief Fill a rectangle, clipped to the display.
 *
 * @param x Left edge.
 * @param y Top edge.
 * @param w Width.
 * @param h Height.
 * @param color Colour (gfx_rgb()).
 */
void gfx_fill(int x, int y, int w, int h, uint16_t color);

/**
 * @brief Draw a line of text in a box one font height tall.
 *
 * The whole box is drawn: text from its left edge, background after it.
 * Text that does not fit is cut.
 *
 * @param x Left edge of the box.
 * @param y Top edge of the box.
 * @param w Box width, clipped to the display.
 * @param font Font (gfx_font()).
 * @param text Text.
 * @param fg Text colour.
 * @param bg Background colour.
 */
void gfx_text(int x, int y, int w, const struct cfb_font *font, const char *text, uint16_t fg,
	      uint16_t bg);

#endif /* METER_GFX_H_ */
