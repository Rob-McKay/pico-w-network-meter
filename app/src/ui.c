/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/display/cfb.h>

#include "gfx.h"
#include "paging.h"
#include "recommend.h"
#include "ui.h"
#include "boot_record.h"
#include "chan_stats.h"

/* Header and footer bars, pixels. */
#define BAR_H          20
/* The UI's colours, as 8-bit red, green and blue for gfx_rgb(). */
#define RGB_BG         0, 0, 0    /* black */
#define RGB_BAR_BG     0, 50, 110 /* dark blue: header and footer */
#define RGB_TEXT       255, 255, 255
#define RGB_DIM        150, 150, 150
#define RGB_GRID       40, 40, 40
#define RGB_GRID_LABEL 80, 80, 80
#define RGB_HIGHLIGHT  0, 210, 255 /* cyan: the recommended channel */
#define RGB_CLEAR      0, 200, 80  /* green */
#define RGB_MODERATE   255, 170, 0 /* amber */
#define RGB_BUSY       230, 40, 40 /* red */

/* The footer's scan count, "scan 4294967295", with its terminator. */
#define SCAN_TEXT_MAX   16U
/* A short number on the chart (a dBm label, a count of networks, a channel), with its
 * terminator and room to spare.
 */
#define NUMBER_TEXT_MAX 8U

/* Height of the text font, pixels. */
#define FONT_H          16
/* Space between text and the edge of its bar, pixels. */
#define PAD             2
/* Width of the chart's dBm labels: "-100" would not fit; "-80" does. */
#define AXIS_W          34
/* Space each side of a bar in its column, pixels. */
#define BAR_GAP         3
/* Width of the strongest network's bar, in the middle of a column, pixels. */
#define SIGNAL_W        6
/* Brightness of the interference bar beside it, out of 255: 60%. */
#define DIM_LEVEL       153
/* Channels on the chart: 1-13 (14 is Japan-only, 802.11b). */
#define CHART_CHANNELS  13
/* The 5 GHz chart: the 80 MHz blocks 36-48 to 149-161 (165-177 is rarely
 * allowed), four 10-pixel columns each, with a gap between blocks: 270 of
 * the 286 pixels beside the axis.
 */
#define CHART_5G_BLOCKS 6
#define COL_5G_W        10
#define BLOCK_GAP       6
/* A 5 GHz column's bars: 1 pixel each side, a 4-pixel bright bar between
 * 2-pixel dim ones.
 */
#define BAR_GAP_5G      1
#define SIGNAL_5G_W     4
/* Range of the chart, dBm. */
#define DBM_MIN         (-100)
#define DBM_MAX         (-30)
/* The chart's grid lines are GRID_STEP_DB apart; every LABEL_STEP_DB is labelled. */
#define GRID_STEP_DB    10
#define LABEL_STEP_DB   20
/* Signal strength boundaries of the network list's colours, dBm. */
#define RSSI_GOOD       (-60)
#define RSSI_FAIR       (-75)
/* A row of the network list, pixels. */
#define ROW_H           20
/* Columns of the network list, in characters: with NAME_GAP, 318 of the
 * panel's 320 pixels in the 10-pixel font.
 */
#define SSID_CHARS      24
#define CHAN_CHARS      3
#define RSSI_CHARS      4
/* Space between a network's name and its channel, pixels. */
#define NAME_GAP        8
/* Longest line of text: the display's width in characters, and a NUL. */
#define LINE_MAX        40

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

/* Whether ui_init() succeeded: until it has, the ui_ functions draw nothing (main thread). */
static bool ready;

/* The events screen's records, from ui_set_events(). */
static size_t events_count;
static ui_event_get_fn events_get;

/**
 * @brief Work out the UI's colours in the display's pixel format.
 *
 * @sideeffects Sets the colour table @c col. errno unchanged.
 */
static void init_colours(void)
{
	col.bg = gfx_rgb(RGB_BG);
	col.bar_bg = gfx_rgb(RGB_BAR_BG);
	col.text = gfx_rgb(RGB_TEXT);
	col.dim = gfx_rgb(RGB_DIM);
	col.grid = gfx_rgb(RGB_GRID);
	col.grid_label = gfx_rgb(RGB_GRID_LABEL);
	col.highlight = gfx_rgb(RGB_HIGHLIGHT);
	col.clear = gfx_rgb(RGB_CLEAR);
	col.moderate = gfx_rgb(RGB_MODERATE);
	col.busy = gfx_rgb(RGB_BUSY);
}


/*
 * Public interface: documented in ui.h.
 *
 * The font is looked up before the panel is started, so a missing font fails without touching
 * the display. gfx_font() falls back to the smallest font, which the layout, built for FONT_H,
 * can't use, so the height is checked here.
 */
int ui_init(void)
{
	int ret;

	ready = false;
	init_colours();
	font = gfx_font(FONT_H);
	if (font == NULL || font->height != FONT_H) {
		return -ENOENT;
	}
	ret = gfx_init(col.bg);
	ready = ret == 0;

	return ret;
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
	char right[SCAN_TEXT_MAX] = "";

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
 * @param bold Bold text.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void text_centred(int x, int y, int w, const char *text, uint16_t fg, uint16_t bg, bool bold)
{
	int off = MAX((w - ((int)strlen(text) * font->width)) / 2, 0);

	gfx_fill(x, y, off, font->height, bg);
	if (bold) {
		gfx_text_bold(x + off, y, w - off, font, text, fg, bg);
	} else {
		gfx_text(x + off, y, w - off, font, text, fg, bg);
	}
}


/* Where the channel chart goes. */
struct chart {
	int top;      /* top of the plot (room for a count label above a bar) */
	int bottom;   /* bottom of the bars */
	int col_w;    /* width of a channel's column */
	int bar_gap;  /* space each side of a column's bars */
	int signal_w; /* width of the bright bar in the middle */
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
	c->bar_gap = BAR_GAP;
	c->signal_w = SIGNAL_W;
	c->bar_max = c->bottom - c->top - FONT_H;
}


/**
 * @brief Work out the 5 GHz chart's layout: as the 2.4 GHz chart's, with
 *        narrower columns.
 *
 * @param[out] c Layout.
 *
 * @sideeffects None (writes only through @p c); errno unchanged.
 */
static void chart_layout_5g(struct chart *c)
{
	chart_layout(c);
	c->col_w = COL_5G_W;
	c->bar_gap = BAR_GAP_5G;
	c->signal_w = SIGNAL_5G_W;
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
		char label[NUMBER_TEXT_MAX];

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
 * A count too wide for the column (10 or more in a 5 GHz column) shows as
 * "+".
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param bar_top Top of the taller of the channel's bars.
 * @param info The channel's figures.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_count(const struct chart *c, int x, int bar_top, const struct chan_info *info)
{
	int label_y = bar_top - FONT_H;
	char text[NUMBER_TEXT_MAX];

	if (info->ap_count == 0U) {
		fill_plot(c, x, c->col_w, c->top, bar_top - c->top);
		return;
	}
	(void)snprintf(text, sizeof(text), "%u", (unsigned int)info->ap_count);
	if ((int)strlen(text) * font->width > c->col_w) {
		(void)snprintf(text, sizeof(text), "+");
	}
	fill_plot(c, x, c->col_w, c->top, label_y - c->top);
	text_centred(x, label_y, c->col_w, text, gfx_fade(col.text, info->freshness), col.bg,
		     false);
}


/* A channel's two bars: their heights in pixels, and colours. */
struct bars {
	int signal;       /* the strongest network on the channel: bright */
	int interference; /* every other network reaching the channel: dim */
	uint16_t bright;
	uint16_t dim;
};

/**
 * @brief Work out a channel's bars from its figures.
 *
 * @param c Layout.
 * @param info The channel's figures: its level gives the colour, its
 *        freshness the brightness.
 * @param[out] b The bars.
 *
 * @sideeffects None (writes only through @p b); errno unchanged.
 */
static void bars_for(const struct chart *c, const struct chan_info *info, struct bars *b)
{
	uint16_t colour = level_colour(chan_stats_level(info->interference));

	b->signal = dbm_length(info->best_rssi, c->bar_max);
	b->interference = dbm_length(info->others, c->bar_max);
	b->bright = gfx_fade(colour, info->freshness);
	b->dim = gfx_fade(b->bright, DIM_LEVEL);
}


/**
 * @brief Draw a vertical strip of a channel's bars, from the top of the
 *        taller one down: the grid, then the dim bar, then the bright one.
 *
 * @param c Layout.
 * @param x Left edge.
 * @param w Width.
 * @param b The bars.
 * @param with_signal The strip is under the bright bar, not just the dim one.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_strip(const struct chart *c, int x, int w, const struct bars *b, bool with_signal)
{
	int top = c->bottom - MAX(b->signal, b->interference);
	int signal = with_signal ? b->signal : 0;
	int dim_top = c->bottom - MAX(signal, b->interference);
	int bright_top = c->bottom - signal;

	fill_plot(c, x, w, top, dim_top - top);
	gfx_fill(x, dim_top, w, bright_top - dim_top, b->dim);
	gfx_fill(x, bright_top, w, signal, b->bright);
}


/**
 * @brief Draw a channel's bars: a bright signal bar in the middle of a dim
 *        interference bar, with the grid around them.
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param b The bars.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_bars(const struct chart *c, int x, const struct bars *b)
{
	int top = c->bottom - MAX(b->signal, b->interference);
	int side_w = (c->col_w - (2 * c->bar_gap) - c->signal_w) / 2;
	int mid = x + c->bar_gap + side_w;
	int right = mid + c->signal_w;
	int end = x + c->col_w - c->bar_gap;

	fill_plot(c, x, c->bar_gap, top, c->bottom - top);
	draw_strip(c, x + c->bar_gap, side_w, b, false);
	draw_strip(c, mid, c->signal_w, b, true);
	draw_strip(c, right, end - right, b, false);
	fill_plot(c, end, x + c->col_w - end, top, c->bottom - top);
}


/**
 * @brief Draw a channel's number under its bar: bold for the standard
 *        channels 1, 6 and 11, highlighted for the recommended one.
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
	char text[NUMBER_TEXT_MAX];

	(void)snprintf(text, sizeof(text), "%u", (unsigned int)channel);
	text_centred(x, c->bottom + PAD, c->col_w, text, recommended ? col.bg : col.text,
		     recommended ? col.highlight : col.bg, chan_is_standard(channel));
}


/**
 * @brief Draw one channel's count and bars, from the top of the plot to
 *        the bottom of the bars.
 *
 * Every pixel of that part of the column is drawn, so it needs no clearing
 * first.
 *
 * @param c Layout.
 * @param x Left edge of the column.
 * @param info The channel's figures.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_plot_column(const struct chart *c, int x, const struct chan_info *info)
{
	struct bars b;

	bars_for(c, info, &b);
	draw_count(c, x, c->bottom - MAX(b.signal, b.interference), info);
	draw_bars(c, x, &b);
}


/**
 * @brief Draw one 2.4 GHz channel's column: count, bars and channel number.
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

	draw_plot_column(c, x, info);
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
 * @brief Left edge of a 5 GHz block on the chart.
 *
 * @param block 0 (36-48) to CHART_5G_BLOCKS - 1 (149-161).
 * @return x: the blocks are centred in the space beside the axis.
 *
 * @sideeffects None; errno unchanged.
 */
static int block_x(int block)
{
	int block_w = CHAN_5G_BLOCK_CHANNELS * COL_5G_W;
	int used = (CHART_5G_BLOCKS * block_w) + ((CHART_5G_BLOCKS - 1) * BLOCK_GAP);
	int x0 = AXIS_W + ((gfx_width() - AXIS_W - used) / 2);

	return x0 + (block * (block_w + BLOCK_GAP));
}


/**
 * @brief Draw a 5 GHz block's label under its four columns: its first
 *        channel, bold without DFS, highlighted if recommended.
 *
 * @param c Layout.
 * @param block 0 to CHART_5G_BLOCKS - 1.
 * @param recommended Highlight it.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_block_label(const struct chart *c, int block, bool recommended)
{
	uint8_t first = chan_5g_channel((size_t)block * CHAN_5G_BLOCK_CHANNELS);
	char text[NUMBER_TEXT_MAX];

	(void)snprintf(text, sizeof(text), "%u", (unsigned int)first);
	text_centred(block_x(block), c->bottom + PAD, CHAN_5G_BLOCK_CHANNELS * COL_5G_W, text,
		     recommended ? col.bg : col.text, recommended ? col.highlight : col.bg,
		     !chan_5g_dfs(first));
}


/**
 * @brief Fill a stretch of the 5 GHz chart between or beside its blocks:
 *        the grid above, background in the label row.
 *
 * @param c Layout.
 * @param x Left edge.
 * @param w Width; 0 or less draws nothing.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_block_gap(const struct chart *c, int x, int w)
{
	if (w <= 0) {
		return;
	}
	fill_plot(c, x, w, c->top, c->bottom - c->top);
	gfx_fill(x, c->bottom + PAD, w, FONT_H, col.bg);
}


/**
 * @brief Draw the 5 GHz channels screen's chart: four columns to a block,
 *        gaps between the blocks, a label under each.
 *
 * @param s Scan statistics.
 * @param full Draw the axis as well (after a clear).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_channels_5g(const struct chan_stats *s, bool full)
{
	struct chart c;
	uint8_t best = recommend_5g(s);
	int block_w = CHAN_5G_BLOCK_CHANNELS * COL_5G_W;

	chart_layout_5g(&c);
	if (full) {
		draw_axis(&c);
	}
	draw_block_gap(&c, AXIS_W, block_x(0) - AXIS_W);
	for (int b = 0; b < CHART_5G_BLOCKS; b++) {
		int x = block_x(b);
		size_t first = (size_t)b * CHAN_5G_BLOCK_CHANNELS;

		for (size_t k = 0; k < CHAN_5G_BLOCK_CHANNELS; k++) {
			struct chan_info info;

			chan_stats_channel_5g(s, chan_5g_channel(first + k), &info);
			draw_plot_column(&c, x + ((int)k * COL_5G_W), &info);
		}
		draw_block_label(&c, b, chan_5g_channel(first) == best);
		draw_block_gap(&c, x + block_w,
			       b + 1 < CHART_5G_BLOCKS ? BLOCK_GAP : gfx_width() - (x + block_w));
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
 * @brief Left edge of the channel column in a network list row: after the
 *        name column and a gap, so a full-length name doesn't run into a
 *        two-digit channel.
 *
 * @return x.
 *
 * @sideeffects None; errno unchanged.
 */
static int list_chan_x(void)
{
	return (SSID_CHARS * font->width) + NAME_GAP;
}


/**
 * @brief Pick the colour of a network's name.
 *
 * @param ap The network.
 * @return Amber if it is open, grey if it is hidden, otherwise white.
 *
 * @sideeffects None; errno unchanged.
 */
static uint16_t name_colour(const struct chan_ap *ap)
{
	if (!ap->secured) {
		return col.moderate;
	}

	return chan_ap_hidden(ap) ? col.dim : col.text;
}


/**
 * @brief Draw a network's name, channel and signal strength as text,
 *        across the whole row.
 *
 * Names are coloured by name_colour() and signal strengths by
 * rssi_colour(); everything fades with the network's freshness.
 *
 * @param y Top edge of the text.
 * @param ap The network.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void draw_network_text(int y, const struct chan_ap *ap)
{
	int x_chan = list_chan_x();
	int x_rssi = x_chan + (CHAN_CHARS * font->width);
	uint8_t f = ap->freshness;
	char text[SSID_CHARS + 1];

	chan_ap_name(ap, text, sizeof(text));
	gfx_text(0, y, x_chan, font, text, gfx_fade(name_colour(ap), f), col.bg);
	/* Right-aligned: in two characters for 2.4 GHz's 1-14, three for 5 GHz's 36-177. */
	(void)snprintf(text, sizeof(text), "%*u",
		       ap->band == CHAN_BAND_5G ? CHAN_CHARS : CHAN_CHARS - 1,
		       (unsigned int)ap->channel);
	gfx_text(x_chan, y, x_rssi - x_chan, font, text, gfx_fade(col.dim, f), col.bg);
	(void)snprintf(text, sizeof(text), "%*d", RSSI_CHARS, ap->rssi);
	gfx_text(x_rssi, y, gfx_width() - x_rssi, font, text, gfx_fade(rssi_colour(ap->rssi), f),
		 col.bg);
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
	gfx_fill(0, y + PAD + FONT_H, w, ROW_H - PAD - FONT_H, col.bg);
}


/*
 * Public interface: documented in ui.h.
 */
size_t ui_list_rows(void)
{
	int rows = (gfx_height() - (2 * BAR_H) - (2 * PAD)) / ROW_H;

	return rows > 0 ? (size_t)rows : 0U;
}


/**
 * @brief Draw a page of the network list.
 *
 * @param s Scan statistics.
 * @param band The band to list (enum chan_band).
 * @param first First network to show, strongest first: the start of a page
 *        (paging_clamp()).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_networks(const struct chan_stats *s, enum chan_band band, size_t first)
{
	const struct chan_ap *list[CHAN_MAX_APS];
	size_t rows = ui_list_rows();
	size_t n = chan_stats_strongest(s, band, list, ARRAY_SIZE(list));

	for (size_t i = 0; i < rows; i++) {
		draw_network(BAR_H + PAD + ((int)i * ROW_H),
			     first + i < n ? list[first + i] : NULL);
	}
}


/**
 * @brief Write the networks screen's footer text: how many networks, and
 *        which are showing when they don't all fit.
 *
 * "Other" counts results on no channel the meter knows, and those that
 * didn't fit; the other band's networks are on its own list. (Without
 * CONFIG_METER_BAND_5G, the scanner leaves 5 GHz results off any channel,
 * so they count as other.)
 *
 * @param s Scan statistics.
 * @param band The band listed (enum chan_band).
 * @param first First network showing.
 * @param[out] buf The text.
 * @param size Size of @p buf.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
static void networks_footer(const struct chan_stats *s, enum chan_band band, size_t first,
			    char *buf, size_t size)
{
	size_t rows = ui_list_rows();
	size_t heard = chan_stats_band_count(s, band);
	unsigned int other = (unsigned int)(s->ignored + s->dropped);

	if (heard <= rows) {
		(void)snprintf(buf, size, "%u heard, %u other", (unsigned int)heard, other);
		return;
	}
	(void)snprintf(buf, size, "%u-%u of %u, %u other", (unsigned int)first + 1U,
		       (unsigned int)MIN(first + rows, heard), (unsigned int)heard, other);
}


/**
 * @brief Say which channel, or on 5 GHz which 80 MHz block, to use and how
 *        busy it is, for the footer: "Use ch 6: clear", "Use 52-64: clear".
 *
 * @param s Statistics.
 * @param band The band (enum chan_band).
 * @param[out] buf The text.
 * @param size Size of @p buf.
 *
 * @sideeffects None (writes only through @p buf). errno may be set by snprintf() (only on an
 *              output error).
 */
static void recommend_text(const struct chan_stats *s, enum chan_band band, char *buf, size_t size)
{
	struct chan_info best;

	if (chan_stats_band_count(s, band) == 0U) {
		(void)snprintf(buf, size, "Nothing heard");
		return;
	}
	if (band == CHAN_BAND_5G) {
		uint8_t first = recommend_5g(s);

		chan_stats_block_5g(s, first, &best);
		(void)snprintf(buf, size, "Use %u-%u: %s", (unsigned int)first,
			       (unsigned int)chan_5g_block_last(first),
			       chan_level_name(chan_stats_level(best.interference)));
		return;
	}
	chan_stats_channel(s, chan_stats_recommend(s), &best);
	(void)snprintf(buf, size, "Use ch %u: %s", (unsigned int)best.channel,
		       chan_level_name(chan_stats_level(best.interference)));
}


/**
 * @brief Draw the whole channels screen: header, chart and footer.
 *
 * @param s Scan statistics.
 * @param band The band (enum chan_band).
 * @param scan_no Scans so far.
 * @param full Draw the axis as well (after a clear).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_channels_screen(const struct chan_stats *s, enum chan_band band,
				 unsigned int scan_no, bool full)
{
	char text[LINE_MAX];

	(void)snprintf(text, sizeof(text), "%u networks",
		       (unsigned int)chan_stats_band_count(s, band));
	if (band == CHAN_BAND_5G) {
		draw_bar(0, "Channels 5 GHz", text);
		show_channels_5g(s, full);
	} else {
		draw_bar(0, "Channels", text);
		show_channels(s, full);
	}
	recommend_text(s, band, text, sizeof(text));
	draw_footer(text, scan_no);
}


/**
 * @brief Draw the whole networks screen: header, a page of the list, and
 *        footer.
 *
 * @param s Scan statistics.
 * @param band The band listed (enum chan_band).
 * @param scan_no Scans so far.
 * @param first First network to show; kept within the list (paging_clamp()).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_networks_screen(const struct chan_stats *s, enum chan_band band,
				 unsigned int scan_no, size_t first)
{
	char text[LINE_MAX];

	first = paging_clamp(first, chan_stats_band_count(s, band), ui_list_rows());
	draw_bar(0, band == CHAN_BAND_5G ? "Networks 5 GHz" : "Networks", "strongest first");
	show_networks(s, band, first);
	networks_footer(s, band, first, text, sizeof(text));
	draw_footer(text, scan_no);
}


/*
 * Public interface: documented in ui.h.
 */
void ui_set_events(size_t count, ui_event_get_fn get)
{
	events_count = get != NULL ? count : 0U;
	events_get = get;
}


/**
 * @brief Pick the colour of an event: how much it matters.
 *
 * @param r The event.
 * @return White for a power-on, amber for a Wi-Fi restart, red for a
 *         crash, a hung thread or an unexplained restart.
 *
 * @sideeffects None; errno unchanged.
 */
static uint16_t event_colour(const struct boot_record *r)
{
	switch (r->cause) {
	case BOOT_POWER_ON:
		return col.text;
	case BOOT_WIFI_RESTART:
		return col.moderate;
	default:
		return col.busy;
	}
}


/**
 * @brief Draw one row of a list as a line of text.
 *
 * Every pixel of the row is drawn, so it needs no clearing first.
 *
 * @param y Top edge.
 * @param text The line, or NULL for an empty row.
 * @param fg Text colour.
 *
 * @sideeffects Draws on the panel. errno unchanged.
 */
static void draw_text_row(int y, const char *text, uint16_t fg)
{
	int w = gfx_width();

	if (text == NULL) {
		gfx_fill(0, y, w, ROW_H, col.bg);
		return;
	}
	gfx_fill(0, y, w, PAD, col.bg);
	gfx_text(0, y + PAD, w, font, text, fg, col.bg);
	gfx_fill(0, y + PAD + FONT_H, w, ROW_H - PAD - FONT_H, col.bg);
}


/**
 * @brief Draw one row of the event list.
 *
 * @param y Top edge.
 * @param r The event, or NULL for an empty row.
 *
 * @sideeffects Draws on the panel (draw_text_row()). errno may be set by snprintf() (only on
 *              an output error).
 */
static void draw_event(int y, const struct boot_record *r)
{
	char text[LINE_MAX];

	if (r == NULL) {
		draw_text_row(y, NULL, col.bg);
		return;
	}
	boot_record_describe(r, text, sizeof(text));
	draw_text_row(y, text, event_colour(r));
}


/**
 * @brief Draw a page of the event list, newest first.
 *
 * With no events, the first row says so.
 *
 * @param first First event to show: the start of a page.
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_events(size_t first)
{
	size_t rows = ui_list_rows();
	int top = BAR_H + PAD;

	for (size_t i = 0; i < rows; i++) {
		size_t n = first + i;
		int y = top + ((int)i * ROW_H);

		if (events_count == 0U && i == 0U) {
			draw_text_row(y, "No events logged", col.dim);
		} else {
			draw_event(y, n < events_count ? events_get(n) : NULL);
		}
	}
}


/**
 * @brief Draw the whole events screen: header, a page of the list, and
 *        footer.
 *
 * @param scan_no Scans so far.
 * @param first First event to show; kept within the list (paging_clamp()).
 *
 * @sideeffects Draws on the panel. errno may be set by snprintf() (only on an output error).
 */
static void show_events_screen(unsigned int scan_no, size_t first)
{
	size_t rows = ui_list_rows();
	char text[LINE_MAX];

	first = paging_clamp(first, events_count, rows);
	draw_bar(0, "Events", "newest first");
	show_events(first);
	if (events_count <= rows) {
		(void)snprintf(text, sizeof(text), "%u starts logged", (unsigned int)events_count);
	} else {
		(void)snprintf(text, sizeof(text), "%u-%u of %u starts", (unsigned int)first + 1U,
			       (unsigned int)MIN(first + rows, events_count),
			       (unsigned int)events_count);
	}
	draw_footer(text, scan_no);
}


/*
 * Public interface: documented in ui.h.
 *
 * A full redraw clears the area between the header and footer first. Otherwise each element
 * draws every pixel it covers, so a redraw doesn't flicker.
 */
void ui_show(enum ui_view view, enum chan_band band, const struct chan_stats *s,
	     unsigned int scan_no, size_t first, bool full)
{
	if (!ready) {
		return;
	}
	if (full) {
		gfx_fill(0, BAR_H, gfx_width(), gfx_height() - (2 * BAR_H), col.bg);
	}
	switch (view) {
	case UI_VIEW_CHANNELS:
		show_channels_screen(s, band, scan_no, full);
		break;
	case UI_VIEW_NETWORKS:
		show_networks_screen(s, band, scan_no, first);
		break;
	default:
		show_events_screen(scan_no, first);
		break;
	}
}


/*
 * Public interface: documented in ui.h.
 */
void ui_message(const char *text)
{
	if (ready) {
		draw_footer(text, 0);
	}
}
