/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The meter's screens (ui.c, gfx.c), drawn on an in-memory 320x240 panel:
 * pixels where the layout puts them, and screenshots of each screen in
 * out/screens/ when scripts/test.sh runs the test.
 */

#include <stdio.h>

#include <zephyr/ztest.h>

#include "chan_stats.h"
#include "fake_display.h"
#include "gfx.h"
#include "ui.h"

/* Layout, as ui.c works it out for 320x240. */
#define BAR_H        20
#define CHART_BOTTOM (240 - BAR_H - 16 - 4)
#define AXIS_W       34
#define COL_W        ((320 - AXIS_W) / 13)
/* Rows of the chart's grid lines at -60 dBm (labelled) and -50 dBm: CHART_BOTTOM less
 * 40 and 50 dB of the 70 dB scale, which is 162 pixels tall.
 */
#define ROW_MINUS_60 (CHART_BOTTOM - ((40 * 162) / 70))
#define ROW_MINUS_50 (CHART_BOTTOM - ((50 * 162) / 70))
#define LIST_TOP     (BAR_H + 2)
#define LIST_BAR_X   (23 * 10)

static struct chan_stats stats;

/**
 * @brief Add an access point to the test scan.
 *
 * @param id Last byte of its BSSID.
 * @param ssid Network name.
 * @param channel Channel.
 * @param rssi dBm.
 * @param secured Not an open network.
 *
 * @sideeffects Adds the access point to the test's @c stats. errno may be set by snprintf()
 *              (only on an output error).
 */
static void add(uint8_t id, const char *ssid, uint8_t channel, int8_t rssi, bool secured)
{
	struct chan_ap ap = {
		.bssid = {0x02, 0, 0, 0, 0, id},
		.channel = channel,
		.rssi = rssi,
		.secured = secured,
		.freshness = CHAN_FRESH,
	};

	(void)snprintf(ap.ssid, sizeof(ap.ssid), "%s", ssid);
	zassert_ok(chan_stats_add(&stats, &ap));
}

/**
 * @brief Start the display and fill in a scan: busy on 1, a little on 4-6,
 *        nothing near 11 (so 11 is recommended).
 *
 * @return NULL: no fixture.
 *
 * @sideeffects Starts the fake panel (ui_init()) and fills the test's @c stats. errno may be
 *              set by snprintf() (only on an output error).
 */
static void *setup(void)
{
	zassert_ok(ui_init());

	chan_stats_reset(&stats);
	add(1, "HomeNetwork", 1, -42, true);
	add(2, "HomeNetwork-Guest", 1, -48, true);
	add(3, "Neighbour 1", 1, -67, true);
	add(4, "CoffeeShop", 6, -81, false);
	add(5, "Printer", 4, -88, true);

	return NULL;
}

/**
 * @brief Left edge of a channel's column on the chart.
 *
 * @param ch Channel.
 * @return x.
 *
 * @sideeffects None; errno unchanged.
 */
static int column_x(int ch)
{
	return AXIS_W + ((ch - 1) * COL_W);
}

/**
 * @brief Bars are coloured by how busy the channel is; empty ones aren't drawn.
 *
 * @sideeffects Draws on the fake panel and saves it to out/screens/channels.ppm. errno may be
 *              set by snprintf() (only on an output error).
 */
ZTEST(meter_ui, test_channel_bars)
{
	ui_show(UI_VIEW_CHANNELS, &stats, 1, true);

	zassert_equal(fake_display_pixel(column_x(1) + (COL_W / 2), CHART_BOTTOM - 1),
		      gfx_rgb(230, 40, 40), "channel 1: busy, red");
	zassert_equal(fake_display_pixel(column_x(6) + (COL_W / 2), CHART_BOTTOM - 1),
		      gfx_rgb(255, 170, 0), "channel 6: moderate, amber");
	zassert_equal(fake_display_pixel(column_x(13) + (COL_W / 2), CHART_BOTTOM - 1),
		      gfx_rgb(0, 0, 0), "channel 13: nothing heard, no bar");
	zassert_equal(fake_display_pixel(column_x(1), CHART_BOTTOM - 1), gfx_rgb(0, 0, 0),
		      "the gap beside a bar is background");
	(void)fake_display_save_ppm("out/screens/channels.ppm");
}

/**
 * @brief The recommended channel's number is highlighted, the others aren't.
 *
 * @sideeffects Draws on the fake panel. errno may be set by snprintf() (only on an output
 *              error).
 */
ZTEST(meter_ui, test_recommended_highlight)
{
	ui_show(UI_VIEW_CHANNELS, &stats, 1, true);

	zassert_equal(chan_stats_recommend(&stats), 11);
	zassert_equal(fake_display_pixel(column_x(11), CHART_BOTTOM + 4), gfx_rgb(0, 210, 255));
	zassert_equal(fake_display_pixel(column_x(6), CHART_BOTTOM + 4), gfx_rgb(0, 0, 0));
}

/**
 * @brief Grid lines cross the empty columns and the gaps beside bars, the
 *        y axis runs down the left of the chart, and a partial redraw keeps
 *        both.
 *
 * @sideeffects Draws on the fake panel. errno may be set by snprintf() (only on an output
 *              error).
 */
ZTEST(meter_ui, test_grid_and_axis)
{
	uint16_t labelled = gfx_rgb(80, 80, 80);
	uint16_t unlabelled = gfx_rgb(40, 40, 40);

	ui_show(UI_VIEW_CHANNELS, &stats, 1, true);
	ui_show(UI_VIEW_CHANNELS, &stats, 2, false);

	zassert_equal(fake_display_pixel(column_x(13) + (COL_W / 2), ROW_MINUS_60), labelled,
		      "grid line across an empty column");
	zassert_equal(fake_display_pixel(column_x(13) + (COL_W / 2), ROW_MINUS_50), unlabelled,
		      "fainter grid line between the labelled ones");
	zassert_equal(fake_display_pixel(column_x(13) + (COL_W / 2), ROW_MINUS_60 + 1),
		      gfx_rgb(0, 0, 0), "background between grid lines");
	zassert_equal(fake_display_pixel(column_x(1), ROW_MINUS_60), labelled,
		      "grid line in the gap beside a bar");
	zassert_equal(fake_display_pixel(column_x(1) + (COL_W / 2), ROW_MINUS_60),
		      gfx_rgb(230, 40, 40), "the bar covers the grid line");
	zassert_equal(fake_display_pixel(AXIS_W - 1, ROW_MINUS_60 + 1), gfx_rgb(150, 150, 150),
		      "y axis");
	zassert_equal(fake_display_pixel(AXIS_W - 1, CHART_BOTTOM - 1), gfx_rgb(150, 150, 150),
		      "y axis reaches the bottom of the bars");
	zassert_equal(fake_display_pixel(AXIS_W - 1, CHART_BOTTOM), gfx_rgb(0, 0, 0),
		      "and stops there");
}

/**
 * @brief The list shows the strongest network first, with a green bar.
 *
 * @sideeffects Draws on the fake panel and saves it to out/screens/networks.ppm. errno may be
 *              set by snprintf() (only on an output error).
 */
ZTEST(meter_ui, test_network_list)
{
	ui_show(UI_VIEW_NETWORKS, &stats, 2, true);

	zassert_equal(fake_display_pixel(LIST_BAR_X + 1, LIST_TOP + 8), gfx_rgb(0, 200, 80),
		      "-42 dBm: green");
	zassert_equal(fake_display_pixel(LIST_BAR_X + 1, LIST_TOP + (4 * 20) + 8),
		      gfx_rgb(230, 40, 40), "5th, -88 dBm: red");
	zassert_equal(fake_display_pixel(LIST_BAR_X + 1, LIST_TOP + (5 * 20) + 8), gfx_rgb(0, 0, 0),
		      "no 6th network");
	(void)fake_display_save_ppm("out/screens/networks.ppm");
}

/**
 * @brief Header and footer bars are drawn edge to edge.
 *
 * @sideeffects Draws on the fake panel. errno may be set by snprintf() (only on an output
 *              error).
 */
ZTEST(meter_ui, test_bars)
{
	uint16_t bar = gfx_rgb(0, 50, 110);

	ui_show(UI_VIEW_CHANNELS, &stats, 3, true);
	zassert_equal(fake_display_pixel(0, 0), bar);
	zassert_equal(fake_display_pixel(319, BAR_H - 1), bar);
	zassert_equal(fake_display_pixel(0, 240 - BAR_H), bar);
	zassert_equal(fake_display_pixel(319, 239), bar);
}

/**
 * @brief Ageing networks are drawn darker; the static labels are not.
 *
 * @sideeffects Overwrites its own static copy of the scan; draws on the fake panel and saves it
 *              to out/screens/faded.ppm. errno may be set by snprintf() (only on an output
 *              error).
 */
ZTEST(meter_ui, test_faded)
{
	static struct chan_stats faded;

	faded = stats;
	for (size_t i = 0; i < faded.ap_count; i++) {
		faded.aps[i].freshness = 128;
	}
	ui_show(UI_VIEW_CHANNELS, &faded, 4, true);
	zassert_equal(fake_display_pixel(column_x(1) + (COL_W / 2), CHART_BOTTOM - 1),
		      gfx_fade(gfx_rgb(230, 40, 40), 128), "channel 1's bar at half brightness");
	zassert_equal(fake_display_pixel(column_x(11), CHART_BOTTOM + 4), gfx_rgb(0, 210, 255),
		      "the recommended channel's highlight does not fade");
	(void)fake_display_save_ppm("out/screens/faded.ppm");
}

ZTEST_SUITE(meter_ui, NULL, setup, NULL, NULL, NULL);
