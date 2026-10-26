/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drawing on the Display Pack's ST7789V panel (zephyr,display, RGB565 high
 * byte first) without a frame buffer: rectangles and lines of text are
 * sent as they are drawn, through one small band buffer. The buffer is
 * shared, so drawing is not thread-safe: draw from one thread (main).
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
 *
 * @sideeffects None; errno unchanged.
 */
uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Darken a colour towards black.
 *
 * @param color Colour (gfx_rgb()).
 * @param level 255 leaves it as it is, 0 makes it black.
 * @return The darkened colour.
 *
 * @sideeffects None; errno unchanged.
 */
uint16_t gfx_fade(uint16_t color, uint8_t level);

/**
 * @brief Start the display: clear it to @p bg and turn it on.
 *
 * @param bg Background colour (gfx_rgb()).
 * @retval 0 Ready.
 * @retval -ENODEV The display is not ready.
 * @retval -ENOTSUP The display is not RGB565 or is wider than supported.
 *
 * @sideeffects Sets the module's display size (@c disp_w, @c disp_h), clears the panel to @p bg
 *              through the shared band buffer, and turns the panel on. errno unchanged.
 */
int gfx_init(uint16_t bg);

/**
 * @brief Display width.
 *
 * @return Pixels.
 *
 * @sideeffects None; errno unchanged.
 */
int gfx_width(void);

/**
 * @brief Display height.
 *
 * @return Pixels.
 *
 * @sideeffects None; errno unchanged.
 */
int gfx_height(void);

/**
 * @brief Find a font by height.
 *
 * @param height Pixels (16 or 24 with the default fonts).
 * @return The font, the smallest font if none has that height, or NULL if
 *         no fonts are linked in (CONFIG_CHARACTER_FRAMEBUFFER off).
 *
 * @sideeffects None; errno unchanged.
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
 *
 * @sideeffects Overwrites the shared band buffer and draws on the panel. errno unchanged.
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
 *
 * @sideeffects Overwrites the shared band buffer and draws on the panel. errno unchanged.
 */
void gfx_text(int x, int y, int w, const struct cfb_font *font, const char *text, uint16_t fg,
	      uint16_t bg);

/**
 * @brief Draw a line of bold text in a box one font height tall.
 *
 * As gfx_text(), in bold (raster_text_bold()).
 *
 * @param x Left edge of the box.
 * @param y Top edge of the box.
 * @param w Box width, clipped to the display.
 * @param font Font (gfx_font()).
 * @param text Text.
 * @param fg Text colour.
 * @param bg Background colour.
 *
 * @sideeffects Overwrites the shared band buffer and draws on the panel. errno unchanged.
 */
void gfx_text_bold(int x, int y, int w, const struct cfb_font *font, const char *text, uint16_t fg,
		   uint16_t bg);

#endif /* METER_GFX_H_ */
