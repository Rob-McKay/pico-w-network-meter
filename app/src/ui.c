/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "gfx.h"
#include "ui.h"

/* Header and footer bars, pixels. */
#define BAR_H          20
/* Height of the text font, pixels. */
#define FONT_H         16
/* Space between text and the edge of its bar, pixels. */
#define PAD            2
/* Width of the chart's dBm labels: "-100" would not fit; "-80" does. */
#define AXIS_W         34
/* Space each side of a bar in its column, pixels. */
#define BAR_GAP        3
/* Channels on the chart: 1-13 (14 is Japan-only, 802.11b). */
#define CHART_CHANNELS 13
/* Range of the chart and of the signal bars, dBm. */
#define DBM_MIN        (-100)
#define DBM_MAX        (-30)
/* The chart's grid lines are GRID_STEP_DB apart; every LABEL_STEP_DB is labelled. */
#define GRID_STEP_DB   10
#define LABEL_STEP_DB  20
/* Signal strength boundaries of the network list's bars, dBm. */
#define RSSI_GOOD      (-60)
#define RSSI_FAIR      (-75)
/* A row of the network list, pixels. */
#define ROW_H          20
/* Columns of the network list, in characters. */
#define SSID_CHARS     15
#define CHAN_CHARS     3
#define RSSI_CHARS     4
/* Longest line of text: the display's width in characters, and a NUL. */
#define LINE_MAX       40

/* Colours, set by ui_init(). The UI runs on the main thread only. */
static struct {
	uint16_t bg;
	uint16_t bar_bg;
	uint16_t text;
	uint16_t dim;
	uint16_t grid;
	uint16_t grid_label;
	uint16_t highlight;
	uint16_t clear;
	uint16_t moderate;
	uint16_t busy;
} col;

static const struct cfb_font *font;

/**
 * @brief Work out the UI's colours in the display's pixel format.
 *
 * @sideeffects Sets the colour table @c col. errno unchanged.
 */
static void init_colours(void)
{
	col.bg = gfx_rgb(0, 0, 0);
	col.bar_bg = gfx_rgb(0, 50, 110);
	col.text = gfx_rgb(255, 255, 255);
	col.dim = gfx_rgb(150, 150, 150);
	col.grid = gfx_rgb(40, 40, 40);
	col.grid_label = gfx_rgb(80, 80, 80);
	col.highlight = gfx_rgb(0, 210, 255);
	col.clear = gfx_rgb(0, 200, 80);
	col.moderate = gfx_rgb(255, 170, 0);
	col.busy = gfx_rgb(230, 40, 40);
}

/*
 * Public interface: documented in ui.h.
 *
 * The font is looked up before the panel is started, so a missing font fails without touching
 * the display.
 */
int ui_init(void)
{
	init_colours();
	font = gfx_font(FONT_H);
	if (font == NULL) {
		return -ENOENT;
	}

	return gfx_init(col.bg);
}

/**
 * @brief Pick the colour of a level.
 *
 * @param level How busy.
 * @return Green, amber or red.
 *
 * @sideeffects None; errno unchanged.
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
 *
 * @sideeffects None; errno unchanged.
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
 *
 * @sideeffects Draws on the panel, through the shared band buffer (gfx.c). errno may be set by
 *              snprintf() (only on an output error).
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
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
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
 *
 * @sideeffects Draws on the panel. errno unchanged.
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
 *
 * @sideeffects None (writes only through @p c); errno unchanged.
 */
static void chart_layout(struct chart *c)
{
	c->top = BAR_H + PAD;
	c->bottom = gfx_height() - BAR_H - FONT_H - (2 * PAD);
	c->col_w = (gfx_width() - AXIS_W) / CHART_CHANNELS;
	c->bar_max = c->bottom - c->top - FONT_H;
}

/**
 * @brief Work out the row of a dBm value on the chart.
 *
 * @param c Layout.
 * @param dbm Value, DBM_MIN to DBM_MAX.
 * @return y: the top row of a bar reaching @p dbm.
 *
 * @sideeffects None; errno unchanged.
 */
static int dbm_row(const struct chart *c, int dbm)
{
	return c->bottom - dbm_length(dbm, c->bar_max);
}

/**
 * @brief Pick the colour of a grid line: brighter where it is labelled.
 *
 * @param dbm The line's value, DBM_MIN or more.
 * @return Colour.
 *
 * @sideeffects None; errno unchanged.
 */
static uint16_t grid_colour(int dbm)
{
	return ((dbm - DBM_MIN) % LABEL_STEP_DB) == 0 ? col.grid_label : col.grid;
}

/**
 * @brief Fill part of a column of the chart with its background: black,
 *        crossed by the grid lines.
 *
 * Each row is drawn once, so a redraw doesn't flicker.
 *
 * @param c Layout.
 * @param x Left edge.
 * @param w Width.
 * @param y Top edge.
 * @param h Height.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void fill_plot(const struct chart *c, int x, int w, int y, int h)
{
	int end = y + h;

	/* Highest value first, so the lines come in order down the panel. */
	for (int dbm = DBM_MAX; dbm > DBM_MIN; dbm -= GRID_STEP_DB) {
		int line = dbm_row(c, dbm);

		if (line >= y && line < end) {
			gfx_fill(x, y, w, line - y, col.bg);
			gfx_fill(x, line, w, 1, grid_colour(dbm));
			y = line + 1;
		}
	}
	gfx_fill(x, y, w, end - y, col.bg);
}

/**
 * @brief Draw the chart's dBm axis: a vertical line, labelled every
 *        LABEL_STEP_DB.
 *
 * The labels sit beside the grid lines that fill_plot() draws; the -100
 * and -30 ends are left unlabelled, as "-100" would not fit.
 *
 * @param c Layout.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_axis(const struct chart *c)
{
	int top = dbm_row(c, DBM_MAX);

	for (int dbm = DBM_MIN + LABEL_STEP_DB; dbm < DBM_MAX; dbm += LABEL_STEP_DB) {
		char label[8];

		(void)snprintf(label, sizeof(label), "%d", dbm);
		gfx_text(0, dbm_row(c, dbm) - (FONT_H / 2), AXIS_W - PAD, font, label, col.dim,
			 col.bg);
	}
	gfx_fill(AXIS_W - 1, top, 1, c->bottom - top, col.dim);
}

/**
 * @brief Draw the space above a channel's bar, with the network count
 *        just above the bar if there are any networks on the channel.
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param bar_h Height of the bar.
 * @param info The channel's figures.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_count(const struct chart *c, int x, int bar_h, const struct chan_info *info)
{
	int bar_top = c->bottom - bar_h;
	int label_y = bar_top - FONT_H;
	char text[8];

	if (info->ap_count == 0U) {
		fill_plot(c, x, c->col_w, c->top, bar_top - c->top);
		return;
	}
	(void)snprintf(text, sizeof(text), "%u", (unsigned int)info->ap_count);
	fill_plot(c, x, c->col_w, c->top, label_y - c->top);
	text_centred(x, label_y, c->col_w, text, gfx_fade(col.text, info->freshness), col.bg);
}

/**
 * @brief Draw a channel's bar, with the gaps either side of it showing the
 *        grid.
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param bar_h Height of the bar.
 * @param info The channel's figures: its level gives the colour, its
 *        freshness the brightness.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_level_bar(const struct chart *c, int x, int bar_h, const struct chan_info *info)
{
	int top = c->bottom - bar_h;
	int bar_w = c->col_w - (2 * BAR_GAP);
	uint16_t colour = level_colour(chan_stats_level(info->interference));

	fill_plot(c, x, BAR_GAP, top, bar_h);
	gfx_fill(x + BAR_GAP, top, bar_w, bar_h, gfx_fade(colour, info->freshness));
	fill_plot(c, x + BAR_GAP + bar_w, c->col_w - BAR_GAP - bar_w, top, bar_h);
}

/**
 * @brief Draw a channel's number under its bar, highlighted if it is the
 *        recommended channel.
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param channel Channel.
 * @param recommended Highlight it.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_channel_number(const struct chart *c, int x, uint8_t channel, bool recommended)
{
	char text[8];

	(void)snprintf(text, sizeof(text), "%u", (unsigned int)channel);
	text_centred(x, c->bottom + PAD, c->col_w, text, recommended ? col.bg : col.text,
		     recommended ? col.highlight : col.bg);
}

/**
 * @brief Draw one channel's column: count, bar and channel number.
 *
 * Every pixel of the column is drawn, so it needs no clearing first.
 *
 * @param c Layout.
 * @param info The channel's figures.
 * @param recommended Highlight the channel number.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_column(const struct chart *c, const struct chan_info *info, bool recommended)
{
	int x = AXIS_W + ((info->channel - CHAN_FIRST) * c->col_w);
	int bar_h = dbm_length(info->interference, c->bar_max);

	draw_count(c, x, bar_h, info);
	draw_level_bar(c, x, bar_h, info);
	draw_channel_number(c, x, info->channel, recommended);
}

/**
 * @brief Draw the channels screen.
 *
 * @param s Scan statistics.
 * @param full Draw the axis as well (after a clear).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
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
 *
 * @sideeffects None; errno unchanged.
 */
static uint16_t rssi_colour(int rssi)
{
	if (rssi >= RSSI_GOOD) {
		return col.clear;
	}

	return rssi >= RSSI_FAIR ? col.moderate : col.busy;
}

/**
 * @brief Left edge of the signal bar in a network list row.
 *
 * @return x.
 *
 * @sideeffects None; errno unchanged.
 */
static int list_bar_x(void)
{
	return (SSID_CHARS + CHAN_CHARS + RSSI_CHARS + 1) * font->width;
}

/**
 * @brief Draw a network's name, channel and RSSI as text.
 *
 * Open networks' names are amber; everything fades with the network's
 * freshness.
 *
 * @param y Top edge of the text.
 * @param ap The network.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_network_text(int y, const struct chan_ap *ap)
{
	int x_chan = SSID_CHARS * font->width;
	int x_rssi = x_chan + (CHAN_CHARS * font->width);
	uint8_t f = ap->freshness;
	char text[SSID_CHARS + 1];

	(void)snprintf(text, sizeof(text), "%s", ap->ssid[0] != '\0' ? ap->ssid : "(hidden)");
	gfx_text(0, y, x_chan, font, text, gfx_fade(ap->secured ? col.text : col.moderate, f),
		 col.bg);
	(void)snprintf(text, sizeof(text), "%*u", CHAN_CHARS - 1, (unsigned int)ap->channel);
	gfx_text(x_chan, y, x_rssi - x_chan, font, text, gfx_fade(col.dim, f), col.bg);
	(void)snprintf(text, sizeof(text), "%*d", RSSI_CHARS, ap->rssi);
	gfx_text(x_rssi, y, list_bar_x() - x_rssi, font, text, gfx_fade(col.text, f), col.bg);
}

/**
 * @brief Draw a network's signal bar, PAD in from the text's top and
 *        bottom, with the space around it.
 *
 * @param y Top edge of the text beside it.
 * @param ap The network.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_signal_bar(int y, const struct chan_ap *ap)
{
	int x = list_bar_x();
	int w = gfx_width() - x;
	int len = dbm_length(ap->rssi, w - PAD);
	int bar_h = FONT_H - (2 * PAD);

	gfx_fill(x, y, w, PAD, col.bg);
	gfx_fill(x, y + PAD, len, bar_h, gfx_fade(rssi_colour(ap->rssi), ap->freshness));
	gfx_fill(x + len, y + PAD, w - len, bar_h, col.bg);
	gfx_fill(x, y + FONT_H - PAD, w, PAD, col.bg);
}

/**
 * @brief Draw one row of the network list.
 *
 * Every pixel of the row is drawn, so it needs no clearing first.
 *
 * @param y Top edge.
 * @param ap The network, or NULL for an empty row.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_network(int y, const struct chan_ap *ap)
{
	int w = gfx_width();

	if (ap == NULL) {
		gfx_fill(0, y, w, ROW_H, col.bg);
		return;
	}
	gfx_fill(0, y, w, PAD, col.bg);
	draw_network_text(y + PAD, ap);
	draw_signal_bar(y + PAD, ap);
	gfx_fill(0, y + PAD + FONT_H, w, ROW_H - PAD - FONT_H, col.bg);
}

/**
 * @brief Draw the networks screen.
 *
 * @param s Scan statistics.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
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

/**
 * @brief Say which channel to use and how busy it is, for the footer.
 *
 * @param s Statistics.
 * @param[out] buf The text.
 * @param size Size of @p buf.
 *
 * @sideeffects None (writes only through @p buf). errno may be set by snprintf() (only on an
 *              output error).
 */
static void recommend_text(const struct chan_stats *s, char *buf, size_t size)
{
	static const char *const level_names[] = {
		[CHAN_LEVEL_CLEAR] = "clear",
		[CHAN_LEVEL_MODERATE] = "moderate",
		[CHAN_LEVEL_BUSY] = "busy",
	};
	struct chan_info best;

	if (s->ap_count == 0U) {
		(void)snprintf(buf, size, "Nothing heard");
		return;
	}
	chan_stats_channel(s, chan_stats_recommend(s), &best);
	(void)snprintf(buf, size, "Use ch %u: %s", (unsigned int)best.channel,
		       level_names[chan_stats_level(best.interference)]);
}

/**
 * @brief Draw the whole channels screen: header, chart and footer.
 *
 * @param s Scan statistics.
 * @param scan_no Scans so far.
 * @param full Draw the axis as well (after a clear).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_channels_screen(const struct chan_stats *s, unsigned int scan_no, bool full)
{
	char text[LINE_MAX];

	(void)snprintf(text, sizeof(text), "%u networks", (unsigned int)s->ap_count);
	draw_bar(0, "Channels", text);
	show_channels(s, full);
	recommend_text(s, text, sizeof(text));
	draw_footer(text, scan_no);
}

/**
 * @brief Draw the whole networks screen: header, list and footer.
 *
 * @param s Scan statistics.
 * @param scan_no Scans so far.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_networks_screen(const struct chan_stats *s, unsigned int scan_no)
{
	char text[LINE_MAX];

	draw_bar(0, "Networks", "strongest first");
	show_networks(s);
	(void)snprintf(text, sizeof(text), "%u heard, %u other", (unsigned int)s->ap_count,
		       (unsigned int)(s->ignored + s->dropped));
	draw_footer(text, scan_no);
}

/*
 * Public interface: documented in ui.h.
 *
 * A full redraw clears the area between the header and footer first. Otherwise each element
 * draws every pixel it covers, so a redraw doesn't flicker.
 */
void ui_show(enum ui_view view, const struct chan_stats *s, unsigned int scan_no, bool full)
{
	if (full) {
		gfx_fill(0, BAR_H, gfx_width(), gfx_height() - (2 * BAR_H), col.bg);
	}
	if (view == UI_VIEW_CHANNELS) {
		show_channels_screen(s, scan_no, full);
	} else {
		show_networks_screen(s, scan_no);
	}
}

/*
 * Public interface: documented in ui.h.
 */
void ui_message(const char *text)
{
	draw_footer(text, 0);
}
