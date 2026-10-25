/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * An in-memory panel for the UI tests (meter,test-display): it keeps every
 * pixel written to it.
 */

#ifndef METER_FAKE_DISPLAY_H_
#define METER_FAKE_DISPLAY_H_

#include <stdint.h>

/**
 * @brief Read a pixel of the fake panel.
 *
 * @param x Column.
 * @param y Row.
 * @return The pixel, RGB565 high byte first, as written.
 */
uint16_t fake_display_pixel(int x, int y);

/**
 * @brief Save the fake panel as a binary PPM image.
 *
 * Writes only to a file that already exists (native_sim's host calls give
 * no way to set a new file's permissions): scripts/test.sh creates them.
 *
 * @param path File on the host.
 * @retval 0 Saved.
 * @retval -ENOENT No such file.
 * @retval -EIO A write failed.
 */
int fake_display_save_ppm(const char *path);

#endif /* METER_FAKE_DISPLAY_H_ */
