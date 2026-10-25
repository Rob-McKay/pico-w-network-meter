/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/dt-bindings/display/panel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#include "gfx.h"
#include "text_raster.h"

LOG_MODULE_REGISTER(gfx, CONFIG_METER_LOG_LEVEL);

/* Widest display supported, in pixels. */
#define MAX_WIDTH 320
/* Tallest font supported, in pixels: a band is one line of text. */
#define MAX_FONT_H 24

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)

/* text_raster.c draws RGB565 high byte first, as the panel takes it over
 * SPI; the ST7789V driver cannot change format at run time.
 */
BUILD_ASSERT(DT_PROP(DISPLAY_NODE, pixel_format) == PANEL_PIXEL_FORMAT_RGB_565X,
	     "zephyr,display: set pixel-format = <PANEL_PIXEL_FORMAT_RGB_565X>");

static const struct device *const display = DEVICE_DT_GET(DISPLAY_NODE);

/* One line of text, or a strip of a filled rectangle. */
static uint16_t band[MAX_WIDTH * MAX_FONT_H];
static int disp_w;
static int disp_h;

uint16_t gfx_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return raster_rgb(r, g, b);
}

int gfx_init(uint16_t bg)
{
	struct display_capabilities caps;

	if (!device_is_ready(display)) {
		LOG_ERR("Display not ready");
		return -ENODEV;
	}
	display_get_capabilities(display, &caps);
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565X || caps.x_resolution > MAX_WIDTH) {
		LOG_ERR("Unsupported display: %ux%u, format %d", caps.x_resolution,
			caps.y_resolution, (int)caps.current_pixel_format);
		return -ENOTSUP;
	}
	disp_w = caps.x_resolution;
	disp_h = caps.y_resolution;

	gfx_fill(0, 0, disp_w, disp_h, bg);

	return display_blanking_off(display);
}

int gfx_width(void)
{
	return disp_w;
}

int gfx_height(void)
{
	return disp_h;
}

const struct cfb_font *gfx_font(int height)
{
	const struct cfb_font *smallest = NULL;

	STRUCT_SECTION_FOREACH(cfb_font, f) {
		if (f->height == height && f->height <= MAX_FONT_H) {
			return f;
		}
		if (smallest == NULL || f->height < smallest->height) {
			smallest = f;
		}
	}

	return smallest;
}

/**
 * @brief Send a block of the band to the display.
 *
 * @param x Left edge.
 * @param y Top edge.
 * @param w Width: the band holds @p w x @p h pixels, row by row.
 * @param h Height.
 */
static void write_band(int x, int y, int w, int h)
{
	struct display_buffer_descriptor desc = {
		.buf_size = (uint32_t)(w * h) * sizeof(band[0]),
		.width = (uint16_t)w,
		.height = (uint16_t)h,
		.pitch = (uint16_t)w,
	};
	int ret = display_write(display, (uint16_t)x, (uint16_t)y, &desc, band);

	if (ret != 0) {
		LOG_WRN("Display write failed: %d", ret);
	}
}

/**
 * @brief Clip a rectangle to the display.
 *
 * @param[in,out] x Left edge.
 * @param[in,out] y Top edge.
 * @param[in,out] w Width.
 * @param[in,out] h Height.
 * @return True if anything is left to draw.
 */
static bool clip(int *x, int *y, int *w, int *h)
{
	if (*x < 0) {
		*w += *x;
		*x = 0;
	}
	if (*y < 0) {
		*h += *y;
		*y = 0;
	}
	*w = MIN(*w, disp_w - *x);
	*h = MIN(*h, disp_h - *y);

	return *w > 0 && *h > 0;
}

void gfx_fill(int x, int y, int w, int h, uint16_t color)
{
	if (!clip(&x, &y, &w, &h)) {
		return;
	}

	int rows = MIN(h, (int)ARRAY_SIZE(band) / w);

	for (int i = 0; i < w * rows; i++) {
		band[i] = color;
	}
	for (int row = 0; row < h; row += rows) {
		write_band(x, y + row, w, MIN(rows, h - row));
	}
}

void gfx_text(int x, int y, int w, const struct cfb_font *font, const char *text, uint16_t fg,
	      uint16_t bg)
{
	int h = font->height;

	if (h > MAX_FONT_H || !clip(&x, &y, &w, &h) || h != font->height) {
		return;
	}

	raster_text(font, text, band, (size_t)w, fg, bg);
	write_band(x, y, w, h);
}
