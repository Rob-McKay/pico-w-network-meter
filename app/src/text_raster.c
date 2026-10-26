/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include <zephyr/sys/util.h>

#include "rgb565.h"
#include "text_raster.h"

/*
 * Public interface: documented in text_raster.h.
 */
uint16_t raster_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return rgb565x(r, g, b);
}

/**
 * @brief Tell whether a glyph pixel is set.
 *
 * @param font Font.
 * @param glyph The glyph's bytes: one column after another, each column
 *        DIV_ROUND_UP(height, 8) bytes, top pixel first.
 * @param x Column in the glyph.
 * @param y Row in the glyph.
 * @return True if the pixel is ink.
 *
 * @sideeffects None; errno unchanged.
 */
static bool glyph_pixel(const struct cfb_font *font, const uint8_t *glyph, unsigned int x,
			unsigned int y)
{
	unsigned int bytes_per_col = DIV_ROUND_UP(font->height, 8U);
	uint8_t byte = glyph[(x * bytes_per_col) + (y / 8U)];
	bool msb_first = ((uint32_t)font->caps & (uint32_t)CFB_FONT_MSB_FIRST) != 0;
	unsigned int bit = msb_first ? 7U - (y % 8U) : y % 8U;

	return (((unsigned int)byte >> bit) & 1U) != 0;
}

/**
 * @brief Find a character's glyph.
 *
 * @param font Font.
 * @param c Character; one the font lacks shows as '?' (or, in a font
 *        without '?', as its first character).
 * @return The glyph's bytes.
 *
 * @sideeffects None; errno unchanged.
 */
static const uint8_t *glyph_of(const struct cfb_font *font, unsigned char c)
{
	size_t glyph_size = (size_t)font->width * DIV_ROUND_UP(font->height, 8U);

	if (c < font->first_char || c > font->last_char) {
		c = ('?' >= font->first_char && '?' <= font->last_char) ? '?' : font->first_char;
	}

	return (const uint8_t *)font->data + ((size_t)(c - font->first_char) * glyph_size);
}

/**
 * @brief Draw one glyph into the band, clipped to its width.
 *
 * @param font Font.
 * @param c Character.
 * @param[out] band Band of pixels.
 * @param width Band width.
 * @param x0 Left edge of the glyph in the band.
 * @param fg Text colour.
 * @param bold Also ink each pixel to the right of the glyph's ink.
 *
 * @sideeffects None (writes only through @p band); errno unchanged.
 */
static void draw_glyph(const struct cfb_font *font, unsigned char c, uint16_t *band, size_t width,
		       size_t x0, uint16_t fg, bool bold)
{
	const uint8_t *glyph = glyph_of(font, c);

	for (unsigned int x = 0; x < font->width && x0 + x < width; x++) {
		for (unsigned int y = 0; y < font->height; y++) {
			if (glyph_pixel(font, glyph, x, y) ||
			    (bold && x > 0U && glyph_pixel(font, glyph, x - 1U, y))) {
				band[(y * width) + x0 + x] = fg;
			}
		}
	}
}

/**
 * @brief Draw a line of text across a band of pixels, plain or bold.
 *
 * @param font Font.
 * @param text Line of text.
 * @param[out] band Pixels: @p width x the font's height.
 * @param width Band width in pixels.
 * @param fg Text colour.
 * @param bg Background colour.
 * @param bold Bold (draw_glyph()).
 *
 * @sideeffects None (writes only through @p band); errno unchanged.
 */
static void draw_line(const struct cfb_font *font, const char *text, uint16_t *band, size_t width,
		      uint16_t fg, uint16_t bg, bool bold)
{
	size_t x = 0;

	for (size_t i = 0; i < width * font->height; i++) {
		band[i] = bg;
	}
	for (const unsigned char *p = (const unsigned char *)text; *p != '\0' && x < width; p++) {
		if ((*p & 0xc0U) == 0x80U) {
			continue; /* UTF-8 continuation: its character is already drawn */
		}
		draw_glyph(font, *p, band, width, x, fg, bold);
		x += font->width;
	}
}

/*
 * Public interface: documented in text_raster.h.
 */
void raster_text(const struct cfb_font *font, const char *text, uint16_t *band, size_t width,
		 uint16_t fg, uint16_t bg)
{
	draw_line(font, text, band, width, fg, bg, false);
}

/*
 * Public interface: documented in text_raster.h.
 */
void raster_text_bold(const struct cfb_font *font, const char *text, uint16_t *band, size_t width,
		      uint16_t fg, uint16_t bg)
{
	draw_line(font, text, band, width, fg, bg, true);
}
