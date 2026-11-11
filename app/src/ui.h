/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The meter's screens on a 320x240 landscape display:
 *
 *   Channels                          Networks
 *   +------------------------------+  +------------------------------+
 *   | Channels  14 networks        |  | Networks  strongest first    |
 *   | -40      3                   |  | MyHomeWiFi       6  -48 ==== |
 *   | -60   2 ##    1              |  | Neighbour       11  -71 ==   |
 *   | -80   ## ## ## ## 1 ...      |  | ...                          |
 *   |  1  2  3  4  5  ... 13       |  |                              |
 *   | Use ch 6: clear     scan 12  |  | 14 heard, 2 other   scan 12  |
 *   +------------------------------+  +------------------------------+
 *
 * Channels: a bar per 2.4 GHz channel (1-13), as tall as its interference
 * (the overlap-weighted signal of every network heard, -100 to -30 dBm),
 * green, amber or red as it gets busier, with the number of networks on
 * the channel above it. The recommended channel's number is highlighted.
 *
 * Networks: the strongest networks with channel, RSSI and a signal bar.
 *
 * Every network carries a freshness (struct chan_ap): bars, counts and
 * list rows are drawn darker as the network's data ages, down to black.
 */

#ifndef METER_UI_H_
#define METER_UI_H_

#include <stdbool.h>
#include <stddef.h>

#include "boot_record.h"
#include "chan_stats.h"

/** A screen. The channels and networks screens show one band (ui_show()). */
enum ui_view {
	UI_VIEW_CHANNELS,
	UI_VIEW_NETWORKS,
	UI_VIEW_EVENTS,
	UI_VIEW_COUNT,
};

/**
 * @brief Start the display and show a blank screen.
 *
 * The UI is not thread-safe: call it, and the other ui_ functions, from
 * one thread (main).
 *
 * If it fails, ui_show() and ui_message() draw nothing, so the meter can
 * run on its console alone.
 *
 * @retval 0 Ready.
 * @retval -ENOENT No font of the height the UI is laid out for.
 * @retval <0 Negative errno from gfx_init().
 *
 * @sideeffects Sets the colour table, the font and whether the UI is ready, clears the panel
 *              and turns it on (gfx_init()). errno unchanged.
 */
int ui_init(void);

/**
 * @brief Draw a screen with a scan's results.
 *
 * Only the parts that hold data are redrawn, without clearing them first,
 * unless @p full: then the whole screen is (after a change of view).
 *
 * The 2.4 GHz screens are titled "Channels" and "Networks", the 5 GHz ones
 * "Channels 5 GHz" and "Networks 5 GHz". The 5 GHz chart shows the 80 MHz
 * blocks 36-48 to 149-161, labelled by their first channel (bold without
 * DFS), the recommended block highlighted.
 *
 * @param view Screen to show.
 * @param band The band of the channels and networks screens (enum
 *        chan_band); unused by the events screen.
 * @param s The scan's statistics.
 * @param scan_no Scans so far, shown in the footer.
 * @param first The networks or events screen's first row, counting from 0:
 *        the strongest network or newest event to show first; kept within
 *        the list (paging_clamp()). Unused by the channels screen.
 * @param full Redraw the whole screen.
 *
 * @sideeffects Draws on the panel, unless ui_init() failed. errno may be set by snprintf() (only
 *              on an output error).
 */
void ui_show(enum ui_view view, enum chan_band band, const struct chan_stats *s,
	     unsigned int scan_no, size_t first, bool full);

/** Gets the events screen's records: 0 for the newest. */
typedef const struct boot_record *(*ui_event_get_fn)(size_t i);

/**
 * @brief Give the events screen its records.
 *
 * @param count How many there are.
 * @param get Gets one, newest first (event_log_get()); NULL for none.
 *
 * @sideeffects Sets the UI's event source (main thread only). errno unchanged.
 */
void ui_set_events(size_t count, ui_event_get_fn get);

/**
 * @brief Count the rows of the networks and events screens: a page.
 *
 * @return Rows, after ui_init().
 *
 * @sideeffects None; errno unchanged.
 */
size_t ui_list_rows(void);

/**
 * @brief Show a status message in the footer until the next ui_show().
 *
 * @param text Message.
 *
 * @sideeffects Draws on the panel, unless ui_init() failed. errno may be set by snprintf() (only
 *              on an output error).
 */
void ui_message(const char *text);

#endif /* METER_UI_H_ */
