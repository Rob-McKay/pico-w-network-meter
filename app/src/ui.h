/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * The meter's screens on the Display Pack (320x240, landscape):
 *
 *   Channels                          Networks
 *   +------------------------------+  +------------------------------+
 *   | Channels  14 networks        |  | Networks  strongest first    |
 *   | -40      3                   |  | MyHomeWiFi       6  -48 ==== |
 *   | -60   2 ##    1              |  | Neighbour       11  -71 ==   |
 *   | -80   ## ## ## ## 1 ...      |  | ...                          |
 *   |  1  2  3  4  5  ... 13       |  |                              |
 *   | Best: ch 6          scan 12  |  | A: channels         scan 12  |
 *   +------------------------------+  +------------------------------+
 *
 * Channels: a bar per 2.4 GHz channel (1-13), as tall as its interference
 * (the overlap-weighted signal of every network heard, -100 to -30 dBm),
 * green, amber or red as it gets busier, with the number of networks on
 * the channel above it. The recommended channel's number is highlighted.
 *
 * Networks: the strongest networks with channel, RSSI and a signal bar.
 */

#ifndef METER_UI_H_
#define METER_UI_H_

#include <stdbool.h>

#include "chan_stats.h"

/** A screen. */
enum ui_view {
	UI_VIEW_CHANNELS,
	UI_VIEW_NETWORKS,
	UI_VIEW_COUNT,
};

/**
 * @brief Start the display and show a blank screen.
 *
 * @retval 0 Ready.
 * @retval <0 Negative errno from gfx_init().
 */
int ui_init(void);

/**
 * @brief Draw a screen with a scan's results.
 *
 * Only the parts that hold data are redrawn, without clearing them first,
 * unless @p full: then the whole screen is (after a change of view).
 *
 * @param view Screen to show.
 * @param s The scan's statistics.
 * @param scan_no Scans so far, shown in the footer.
 * @param full Redraw the whole screen.
 */
void ui_show(enum ui_view view, const struct chan_stats *s, unsigned int scan_no, bool full);

/**
 * @brief Show a status message in the footer until the next ui_show().
 *
 * @param text Message.
 */
void ui_message(const char *text);

#endif /* METER_UI_H_ */
