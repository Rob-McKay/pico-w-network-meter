/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT meter_test_display

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>

#include <nsi_host_trampolines.h>

#include "fake_display.h"

#define WIDTH DT_INST_PROP(0, width)
#define HEIGHT DT_INST_PROP(0, height)

/* Host open() flags for an existing file: O_WRONLY | O_TRUNC on Linux, the
 * only host the tests run on (the Docker image).
 */
#define HOST_O_WRONLY_TRUNC 01001

static uint16_t pixels[WIDTH * HEIGHT];

/**
 * @brief Copy a block of pixels to the panel.
 *
 * @param dev The panel.
 * @param x Left edge.
 * @param y Top edge.
 * @param desc Size and pitch of @p buf.
 * @param buf Pixels.
 * @retval 0 Written.
 * @retval -EINVAL The block is not on the panel.
 */
static int fake_write(const struct device *dev, const uint16_t x, const uint16_t y,
		      const struct display_buffer_descriptor *desc, const void *buf)
{
	const uint16_t *src = buf;

	ARG_UNUSED(dev);
	if (x + desc->width > WIDTH || y + desc->height > HEIGHT) {
		return -EINVAL;
	}
	for (uint16_t row = 0; row < desc->height; row++) {
		memcpy(&pixels[((size_t)(y + row) * WIDTH) + x], &src[(size_t)row * desc->pitch],
		       desc->width * sizeof(pixels[0]));
	}

	return 0;
}

/**
 * @brief Describe the panel.
 *
 * @param dev The panel.
 * @param[out] caps Its size and pixel format.
 */
static void fake_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	ARG_UNUSED(dev);
	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = WIDTH;
	caps->y_resolution = HEIGHT;
	caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565X;
	caps->current_pixel_format = PIXEL_FORMAT_RGB_565X;
}

/**
 * @brief Turn the panel on: nothing to do.
 *
 * @param dev The panel.
 * @return 0.
 */
static int fake_blanking_off(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static DEVICE_API(display, fake_api) = {
	.write = fake_write,
	.get_capabilities = fake_get_capabilities,
	.blanking_off = fake_blanking_off,
};

/* Zephyr's definition macro can end in an empty statement: ISO C -Wpedantic
 * flags the ';' as extra. Nothing else is exempted.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,
		      &fake_api);
#pragma GCC diagnostic pop

uint16_t fake_display_pixel(int x, int y)
{
	return pixels[(y * WIDTH) + x];
}

int fake_display_save_ppm(const char *path)
{
	char header[32];
	uint8_t row[WIDTH * 3];
	int fd = nsi_host_open(path, HOST_O_WRONLY_TRUNC);
	int len = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", WIDTH, HEIGHT);
	int ret = 0;

	if (fd < 0) {
		return -ENOENT;
	}
	if (nsi_host_write(fd, header, (unsigned long)len) != len) {
		ret = -EIO;
	}
	for (int y = 0; y < HEIGHT && ret == 0; y++) {
		for (int x = 0; x < WIDTH; x++) {
			unsigned int px = sys_be16_to_cpu(pixels[(y * WIDTH) + x]);

			row[(x * 3) + 0] = (uint8_t)((px >> 8U) & 0xf8U);
			row[(x * 3) + 1] = (uint8_t)((px >> 3U) & 0xfcU);
			row[(x * 3) + 2] = (uint8_t)((px << 3U) & 0xf8U);
		}
		if (nsi_host_write(fd, row, sizeof(row)) != (long)sizeof(row)) {
			ret = -EIO;
		}
	}
	(void)nsi_host_close(fd);

	return ret;
}
