/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "gfx.h"
#include "ui.h"

/* Header and footer bars, pixels. */
#define BAR_H 20
/* Height of the text font, pixels. */
#define FONT_H 16
/* Space between text and the edge of its bar, pixels. */
#define PAD 2
/* Width of the chart's dBm labels: "-100" would not fit; "-80" does. */
#define AXIS_W 34
/* Space each side of a bar in its column, pixels. */
#define BAR_GAP 3
/* Channels on the chart: 1-13 (14 is Japan-only, 802.11b). */
#define CHART_CHANNELS 13
/* Range of the chart and of the signal bars, dBm. */
#define DBM_MIN (-100)
#define DBM_MAX (-30)
/* Signal strength boundaries of the network list's bars, dBm. */
#define RSSI_GOOD (-60)
#define RSSI_FAIR (-75)
/* A row of the network list, pixels. */
#define ROW_H 20
/* Columns of the network list, in characters. */
#define SSID_CHARS 15
#define CHAN_CHARS 3
#define RSSI_CHARS 4
/* Longest line of text: the display's width in characters, and a NUL. */
#define LINE_MAX 40

/* Colours, set by ui_init(). */
static struct {
	uint16_t bg;
	uint16_t bar_bg;
	uint16_t text;
	uint16_t dim;
	uint16_t highlight;
	uint16_t clear;
	uint16_t moderate;
	uint16_t busy;
} col;

static const struct cfb_font *font;

int ui_init(void)
{
	col.bg = gfx_rgb(0, 0, 0);
	col.bar_bg = gfx_rgb(0, 50, 110);
	col.text = gfx_rgb(255, 255, 255);
	col.dim = gfx_rgb(150, 150, 150);
	col.highlight = gfx_rgb(0, 210, 255);
	col.clear = gfx_rgb(0, 200, 80);
	col.moderate = gfx_rgb(255, 170, 0);
	col.busy = gfx_rgb(230, 40, 40);
	font = gfx_font(FONT_H);

	return gfx_init(col.bg);
}

/**
 * @brief Pick the colour of a level.
 *
 * @param level How busy.
 * @return Green, amber or red.
 */
static uint16_t level_colour(enum chan_level level)
{
	switch (level) {
	case CHAN_LEVEL_BUSY:
		return col.busy;
	case CHAN_LEVEL_MODERATE:
		return col.moderate;
	default:
		return col.clear;
	}
}

/**
 * @brief Scale a dBm value to a length.
 *
 * @param dbm Value; below DBM_MIN (or CHAN_RSSI_NONE) gives 0, above
 *        DBM_MAX the full length.
 * @param full Length for DBM_MAX.
 * @return 0 to @p full.
 */
static int dbm_length(int dbm, int full)
{
	int v = CLAMP(dbm, DBM_MIN, DBM_MAX) - DBM_MIN;

	return (v * full) / (DBM_MAX - DBM_MIN);
}

/**
 * @brief Draw a header or footer bar: text on the left and the right.
 *
 * @param y Top edge.
 * @param left Text from the left edge.
 * @param right Text ending at the right edge.
 */
static void draw_bar(int y, const char *left, const char *right)
{
	char line[LINE_MAX];
	int cols = MIN((gfx_width() - (2 * PAD)) / font->width, LINE_MAX - 1);
	int right_len = (int)strlen(right);

	(void)snprintf(line, sizeof(line), "%-*.*s%s", MAX(cols - right_len, 0),
		       MAX(cols - right_len, 0), left, right);
	gfx_fill(0, y, gfx_width(), PAD, col.bar_bg);
	gfx_fill(0, y, PAD, BAR_H, col.bar_bg);
	gfx_text(PAD, y + PAD, gfx_width() - PAD, font, line, col.text, col.bar_bg);
	gfx_fill(0, y + PAD + FONT_H, gfx_width(), BAR_H - PAD - FONT_H, col.bar_bg);
}

/**
 * @brief Draw the footer: a message and the scan count.
 *
 * @param left Message.
 * @param scan_no Scans so far, or 0 to leave it out.
 */
static void draw_footer(const char *left, unsigned int scan_no)
{
	char right[16] = "";

	if (scan_no > 0U) {
		(void)snprintf(right, sizeof(right), "scan %u", scan_no);
	}
	draw_bar(gfx_height() - BAR_H, left, right);
}

/**
 * @brief Draw text centred in a column, filling the rest of it.
 *
 * @param x Left edge of the column.
 * @param y Top edge.
 * @param w Column width.
 * @param text Text.
 * @param fg Text colour.
 * @param bg Background colour.
 */
static void text_centred(int x, int y, int w, const char *text, uint16_t fg, uint16_t bg)
{
	int off = MAX((w - ((int)strlen(text) * font->width)) / 2, 0);

	gfx_fill(x, y, off, font->height, bg);
	gfx_text(x + off, y, w - off, font, text, fg, bg);
}

/* Where the channel chart goes. */
struct chart {
	int top;    /* top of the plot (room for a count label above a bar) */
	int bottom; /* bottom of the bars */
	int col_w;  /* width of a channel's column */
	int bar_max;
};

/**
 * @brief Work out the channel chart's layout from the display size.
 *
 * @param[out] c Layout.
 */
static void chart_layout(struct chart *c)
{
	c->top = BAR_H + PAD;
	c->bottom = gfx_height() - BAR_H - FONT_H - (2 * PAD);
	c->col_w = (gfx_width() - AXIS_W) / CHART_CHANNELS;
	c->bar_max = c->bottom - c->top - FONT_H;
}

/**
 * @brief Draw the chart's dBm axis labels.
 *
 * @param c Layout.
 */
static void draw_axis(const struct chart *c)
{
	for (int dbm = -40; dbm >= -80; dbm -= 20) {
		char label[8];
		int y = c->bottom - dbm_length(dbm, c->bar_max) - (FONT_H / 2);

		(void)snprintf(label, sizeof(label), "%d", dbm);
		gfx_text(0, y, AXIS_W - PAD, font, label, col.dim, col.bg);
	}
}

/**
 * @brief Draw one channel's column: count label, bar and channel number.
 *
 * Every pixel of the column is drawn, so it needs no clearing first.
 *
 * @param c Layout.
 * @param info The channel's figures.
 * @param recommended Highlight the channel number.
 */
static void draw_column(const struct chart *c, const struct chan_info *info, bool recommended)
{
	int x = AXIS_W + ((info->channel - CHAN_FIRST) * c->col_w);
	int bar_h = dbm_length(info->interference, c->bar_max);
	int label_y = c->bottom - bar_h - FONT_H;
	int bar_w = c->col_w - (2 * BAR_GAP);
	char text[8] = "";

	if (info->ap_count > 0U) {
		(void)snprintf(text, sizeof(text), "%u", info->ap_count);
	}
	gfx_fill(x, c->top, c->col_w, label_y - c->top, col.bg);
	text_centred(x, label_y, c->col_w, text, col.text, col.bg);
	gfx_fill(x, c->bottom - bar_h, BAR_GAP, bar_h, col.bg);
	gfx_fill(x + BAR_GAP, c->bottom - bar_h, bar_w, bar_h,
		 level_colour(chan_stats_level(info->interference)));
	gfx_fill(x + BAR_GAP + bar_w, c->bottom - bar_h, c->col_w - BAR_GAP - bar_w, bar_h, col.bg);

	(void)snprintf(text, sizeof(text), "%u", info->channel);
	text_centred(x, c->bottom + PAD, c->col_w, text, recommended ? col.bg : col.text,
		     recommended ? col.highlight : col.bg);
}

/**
 * @brief Draw the channels screen.
 *
 * @param s Scan statistics.
 * @param full Draw the axis labels as well (after a clear).
 */
static void show_channels(const struct chan_stats *s, bool full)
{
	struct chart c;
	uint8_t best = chan_stats_recommend(s);

	chart_layout(&c);
	if (full) {
		draw_axis(&c);
	}
	for (uint8_t ch = CHAN_FIRST; ch < CHAN_FIRST + CHART_CHANNELS; ch++) {
		struct chan_info info;

		chan_stats_channel(s, ch, &info);
		draw_column(&c, &info, ch == best);
	}
}

/**
 * @brief Pick the colour of a signal strength.
 *
 * @param rssi dBm.
 * @return Green, amber or red.
 */
static uint16_t rssi_colour(int rssi)
{
	if (rssi >= RSSI_GOOD) {
		return col.clear;
	}

	return rssi >= RSSI_FAIR ? col.moderate : col.busy;
}

/**
 * @brief Draw one row of the network list: SSID, channel, RSSI and bar.
 *
 * Open networks' names are amber. Every pixel of the row is drawn.
 *
 * @param y Top edge.
 * @param ap The network, or NULL for an empty row.
 */
static void draw_network(int y, const struct chan_ap *ap)
{
	int w = gfx_width();
	int x_chan = SSID_CHARS * font->width;
	int x_rssi = x_chan + (CHAN_CHARS * font->width);
	int x_bar = x_rssi + ((RSSI_CHARS + 1) * font->width);
	int len;
	char text[SSID_CHARS + 1];

	if (ap == NULL) {
		gfx_fill(0, y, w, ROW_H, col.bg);
		return;
	}

	gfx_fill(0, y, w, PAD, col.bg);
	(void)snprintf(text, sizeof(text), "%s", ap->ssid[0] != '\0' ? ap->ssid : "(hidden)");
	gfx_text(0, y + PAD, x_chan, font, text, ap->secured ? col.text : col.moderate, col.bg);
	(void)snprintf(text, sizeof(text), "%*u", CHAN_CHARS - 1, ap->channel);
	gfx_text(x_chan, y + PAD, x_rssi - x_chan, font, text, col.dim, col.bg);
	(void)snprintf(text, sizeof(text), "%*d", RSSI_CHARS, ap->rssi);
	gfx_text(x_rssi, y + PAD, x_bar - x_rssi, font, text, col.text, col.bg);

	/* The bar, PAD in from the text's top and bottom. */
	len = dbm_length(ap->rssi, w - x_bar - PAD);
	gfx_fill(x_bar, y + PAD, w - x_bar, PAD, col.bg);
	gfx_fill(x_bar, y + (2 * PAD), len, FONT_H - (2 * PAD), rssi_colour(ap->rssi));
	gfx_fill(x_bar + len, y + (2 * PAD), w - x_bar - len, FONT_H - (2 * PAD), col.bg);
	gfx_fill(x_bar, y + FONT_H, w - x_bar, PAD, col.bg);

	gfx_fill(0, y + PAD + FONT_H, w, ROW_H - PAD - FONT_H, col.bg);
}

/**
 * @brief Draw the networks screen.
 *
 * @param s Scan statistics.
 */
static void show_networks(const struct chan_stats *s)
{
	const struct chan_ap *list[CHAN_MAX_APS];
	int top = BAR_H + PAD;
	int rows = (gfx_height() - (2 * BAR_H) - (2 * PAD)) / ROW_H;
	size_t n = chan_stats_strongest(s, list, MIN((size_t)rows, ARRAY_SIZE(list)));

	for (int i = 0; i < rows; i++) {
		draw_network(top + (i * ROW_H), (size_t)i < n ? list[i] : NULL);
	}
}

void ui_show(enum ui_view view, const struct chan_stats *s, unsigned int scan_no, bool full)
{
	char left[LINE_MAX];

	if (full) {
		gfx_fill(0, BAR_H, gfx_width(), gfx_height() - (2 * BAR_H), col.bg);
	}

	(void)snprintf(left, sizeof(left), "%s", view == UI_VIEW_CHANNELS ? "Channels" : "Networks");
	if (view == UI_VIEW_CHANNELS) {
		char right[24];

		(void)snprintf(right, sizeof(right), "%u networks", (unsigned int)s->ap_count);
		draw_bar(0, left, right);
		show_channels(s, full);
		(void)snprintf(left, sizeof(left), "Best: ch %u", chan_stats_recommend(s));
	} else {
		draw_bar(0, left, "strongest first");
		show_networks(s);
		(void)snprintf(left, sizeof(left), "%u heard, %u other", (unsigned int)s->ap_count,
			       (unsigned int)(s->ignored + s->dropped));
	}
	draw_footer(left, scan_no);
}

void ui_message(const char *text)
{
	draw_footer(text, 0);
}
