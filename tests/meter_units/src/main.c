/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the network meter's hardware-independent code: the
 * channel statistics (chan_stats.c) and the text rasteriser
 * (text_raster.c).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "chan_stats.h"
#include "text_raster.h"

/* ---- Channel statistics -------------------------------------------------- */

/**
 * @brief Make an access point record.
 *
 * @param id Distinguishes BSSIDs (the last byte) and names ("ap<id>").
 * @param channel Channel.
 * @param rssi dBm.
 * @return The record.
 */
static struct chan_ap make_ap(uint8_t id, uint8_t channel, int8_t rssi)
{
	struct chan_ap ap = {
		.bssid = {0x02, 0x00, 0x00, 0x00, 0x00, id},
		.channel = channel,
		.rssi = rssi,
		.secured = true,
	};

	(void)snprintf(ap.ssid, sizeof(ap.ssid), "ap%u", id);

	return ap;
}

/**
 * @brief Add an access point, failing the test if it is refused.
 *
 * @param[in,out] s Statistics.
 * @param id See make_ap().
 * @param channel Channel.
 * @param rssi dBm.
 */
static void add_ap(struct chan_stats *s, uint8_t id, uint8_t channel, int8_t rssi)
{
	struct chan_ap ap = make_ap(id, channel, rssi);

	zassert_ok(chan_stats_add(s, &ap));
}

/** @brief dBm to linear power and back gives the same whole dBm, -130 to 0. */
ZTEST(meter_chan, test_power_round_trip)
{
	for (int dbm = -130; dbm <= 0; dbm++) {
		zassert_equal(chan_power_to_dbm(chan_dbm_to_power(dbm)), dbm, "%d dBm", dbm);
	}
	zassert_equal(chan_dbm_to_power(-130), 10U);
	zassert_equal(chan_dbm_to_power(-100), 10000U);
	zassert_equal(chan_dbm_to_power(-200), 10U, "clamped to -130 dBm");
	zassert_equal(chan_dbm_to_power(10), chan_dbm_to_power(0), "clamped to 0 dBm");
	zassert_equal(chan_power_to_dbm(0), CHAN_RSSI_NONE);
}

/** @brief Twice the power is 3 dB more; ten times is 10 dB. */
ZTEST(meter_chan, test_power_sums)
{
	uint64_t p = chan_dbm_to_power(-60);

	zassert_equal(chan_power_to_dbm(p * 2U), -57);
	zassert_equal(chan_power_to_dbm(p * 10U), -50);
}

/** @brief Every network at 0 dBm on one channel: a large sum still converts. */
ZTEST(meter_chan, test_power_large)
{
	static struct chan_stats s;
	struct chan_info info;

	chan_stats_reset(&s);
	for (uint8_t i = 0; i < CHAN_MAX_APS; i++) {
		add_ap(&s, i, 6, 0);
	}
	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.interference, 18, "64 x 1 mW is 18 dBm");
}

/** @brief A BSSID heard twice is kept once, at its stronger signal. */
ZTEST(meter_chan, test_add_dedupes)
{
	static struct chan_stats s;

	chan_stats_reset(&s);
	add_ap(&s, 1, 6, -70);
	add_ap(&s, 1, 6, -60);
	add_ap(&s, 1, 6, -80);
	add_ap(&s, 2, 6, -75);
	zassert_equal(s.ap_count, 2);
	zassert_equal(s.aps[0].rssi, -60);
	zassert_equal(s.aps[1].rssi, -75);
}

/** @brief Results off the 2.4 GHz channels are counted, not kept. */
ZTEST(meter_chan, test_add_ignores_other_channels)
{
	static struct chan_stats s;
	struct chan_ap none = make_ap(1, 0, -50);
	struct chan_ap high = make_ap(2, CHAN_LAST + 1, -50);

	chan_stats_reset(&s);
	zassert_equal(chan_stats_add(&s, &none), -EINVAL);
	zassert_equal(chan_stats_add(&s, &high), -EINVAL);
	zassert_equal(s.ap_count, 0);
	zassert_equal(s.ignored, 2);
}

/** @brief Past CHAN_MAX_APS access points, the rest are counted as dropped. */
ZTEST(meter_chan, test_add_full)
{
	static struct chan_stats s;
	struct chan_ap extra = make_ap(CHAN_MAX_APS, 1, -50);

	chan_stats_reset(&s);
	for (uint8_t i = 0; i < CHAN_MAX_APS; i++) {
		add_ap(&s, i, 1, -60);
	}
	zassert_equal(chan_stats_add(&s, &extra), -ENOSPC);
	zassert_equal(s.dropped, 1);
	zassert_equal(s.ap_count, CHAN_MAX_APS);
	add_ap(&s, 0, 1, -40); /* a known BSSID still updates */
	zassert_equal(s.aps[0].rssi, -40);
}

/**
 * @brief A network counts on its own channel and interferes, less and less,
 *        up to three channels either side.
 */
ZTEST(meter_chan, test_channel_overlap)
{
	static struct chan_stats s;
	struct chan_info info;
	/* Overlap of 20, 15, 10, 5 and 0 MHz: -50 dBm, then 3/4, 1/2, 1/4, none */
	static const int16_t expect[] = {-50, -51, -53, -56, CHAN_RSSI_NONE};

	chan_stats_reset(&s);
	add_ap(&s, 1, 6, -50);

	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.channel, 6);
	zassert_equal(info.ap_count, 1);
	zassert_equal(info.best_rssi, -50);
	for (int d = 0; d < (int)ARRAY_SIZE(expect); d++) {
		chan_stats_channel(&s, 6 + d, &info);
		zassert_equal(info.interference, expect[d], "ch %d", 6 + d);
		chan_stats_channel(&s, 6 - d, &info);
		zassert_equal(info.interference, expect[d], "ch %d", 6 - d);
	}
	chan_stats_channel(&s, 7, &info);
	zassert_equal(info.ap_count, 0, "neighbours don't count it");
	zassert_equal(info.best_rssi, CHAN_RSSI_NONE);
}

/** @brief Channel 14 sits 12 MHz above 13, not 5. */
ZTEST(meter_chan, test_channel_14)
{
	static struct chan_stats s;
	struct chan_info info;

	chan_stats_reset(&s);
	add_ap(&s, 1, 14, -50);
	chan_stats_channel(&s, 13, &info);
	zassert_equal(info.interference, -54, "8 MHz of 20 overlap");
	chan_stats_channel(&s, 12, &info);
	zassert_equal(info.interference, -58, "17 MHz apart: 3 MHz overlap");
	chan_stats_channel(&s, 11, &info);
	zassert_equal(info.interference, CHAN_RSSI_NONE, "22 MHz apart");
}

/** @brief Signals on one channel add up; the strongest is reported. */
ZTEST(meter_chan, test_channel_sum)
{
	static struct chan_stats s;
	struct chan_info info;

	chan_stats_reset(&s);
	add_ap(&s, 1, 11, -60);
	add_ap(&s, 2, 11, -60);
	add_ap(&s, 3, 11, -80);
	chan_stats_channel(&s, 11, &info);
	zassert_equal(info.ap_count, 3);
	zassert_equal(info.best_rssi, -60);
	zassert_equal(info.interference, -57);
}

/** @brief The quietest of 1, 6 and 11 is recommended; ties go to fewer networks, then 1. */
ZTEST(meter_chan, test_recommend)
{
	static struct chan_stats s;

	chan_stats_reset(&s);
	zassert_equal(chan_stats_recommend(&s), 1, "nothing heard: channel 1");

	add_ap(&s, 1, 1, -50);
	add_ap(&s, 2, 6, -60);
	add_ap(&s, 3, 11, -70);
	zassert_equal(chan_stats_recommend(&s), 11);

	/* Channel 10 leaks into 11 (15 MHz) but not into 6 (20 MHz apart) */
	add_ap(&s, 4, 10, -40);
	zassert_equal(chan_stats_recommend(&s), 6);

	/* -57 dBm on both 1 and 11, but 1 has two networks */
	chan_stats_reset(&s);
	add_ap(&s, 1, 6, -40);
	add_ap(&s, 2, 1, -60);
	add_ap(&s, 3, 1, -60);
	add_ap(&s, 4, 11, -57);
	zassert_equal(chan_stats_recommend(&s), 11);
}

/** @brief Networks are listed strongest first, cut to the room given. */
ZTEST(meter_chan, test_strongest)
{
	static struct chan_stats s;
	const struct chan_ap *list[3];
	static const int8_t rssi[] = {-80, -40, -90, -60, -50, -70};

	chan_stats_reset(&s);
	for (size_t i = 0; i < ARRAY_SIZE(rssi); i++) {
		add_ap(&s, (uint8_t)i, 1, rssi[i]);
	}

	zassert_equal(chan_stats_strongest(&s, list, ARRAY_SIZE(list)), 3);
	zassert_equal(list[0]->rssi, -40);
	zassert_equal(list[1]->rssi, -50);
	zassert_equal(list[2]->rssi, -60);

	zassert_equal(chan_stats_strongest(&s, list, 1), 1);
	zassert_equal(list[0]->rssi, -40);

	chan_stats_reset(&s);
	zassert_equal(chan_stats_strongest(&s, list, ARRAY_SIZE(list)), 0);
}

/** @brief Interference levels split at -85 and -70 dBm. */
ZTEST(meter_chan, test_level)
{
	zassert_equal(chan_stats_level(CHAN_RSSI_NONE), CHAN_LEVEL_CLEAR);
	zassert_equal(chan_stats_level(-86), CHAN_LEVEL_CLEAR);
	zassert_equal(chan_stats_level(-85), CHAN_LEVEL_MODERATE);
	zassert_equal(chan_stats_level(-71), CHAN_LEVEL_MODERATE);
	zassert_equal(chan_stats_level(-70), CHAN_LEVEL_BUSY);
	zassert_equal(chan_stats_level(-20), CHAN_LEVEL_BUSY);
}

ZTEST_SUITE(meter_chan, NULL, NULL, NULL, NULL, NULL);

/* ---- Text on a colour display -------------------------------------------- */

/*
 * A 2x9 font of '?', '@', 'A' and 'B', vertically packed like the CFB
 * fonts: each column is two bytes, bit 0 of the first byte at the top, and
 * bit 0 of the second byte is row 8.
 *   '?': column 0 rows 0 and 8, column 1 empty
 *   'A': column 0 row 1, column 1 row 2
 *   'B': column 1 rows 0 and 8
 */
static const uint8_t tiny_glyphs[4][4] = {
	{ 0x01, 0x01, 0x00, 0x00 }, /* ? */
	{ 0x00, 0x00, 0x00, 0x00 }, /* @ */
	{ 0x02, 0x00, 0x04, 0x00 }, /* A */
	{ 0x00, 0x00, 0x01, 0x01 }, /* B */
};
static const struct cfb_font tiny = {
	.data = tiny_glyphs,
	.caps = CFB_FONT_MONO_VPACKED,
	.width = 2,
	.height = 9,
	.first_char = '?',
	.last_char = 'B',
};

#define FG 0x1111
#define BG 0x2222

/**
 * @brief Read a pixel of a band.
 *
 * @param band Band.
 * @param width Band width.
 * @param x Column.
 * @param y Row.
 * @return Pixel.
 */
static uint16_t px(const uint16_t *band, size_t width, size_t x, size_t y)
{
	return band[(y * width) + x];
}

/** @brief RGB565, stored big-endian as the panel takes it. */
ZTEST(meter_raster, test_rgb)
{
	uint16_t red = raster_rgb(255, 0, 0);
	uint16_t green = raster_rgb(0, 255, 0);
	uint16_t blue = raster_rgb(0, 0, 255);
	const uint8_t *r = (const uint8_t *)&red;
	const uint8_t *g = (const uint8_t *)&green;
	const uint8_t *b = (const uint8_t *)&blue;

	zassert_true(r[0] == 0xf8 && r[1] == 0x00);
	zassert_true(g[0] == 0x07 && g[1] == 0xe0);
	zassert_true(b[0] == 0x00 && b[1] == 0x1f);
	zassert_equal(raster_rgb(255, 255, 255), 0xffff);
}

/** @brief Glyphs land where the font says, column after column. */
ZTEST(meter_raster, test_glyphs)
{
	uint16_t band[6 * 9];

	raster_text(&tiny, "AB", band, 6, FG, BG);
	zassert_equal(px(band, 6, 0, 1), FG, "A: column 0, row 1");
	zassert_equal(px(band, 6, 1, 2), FG, "A: column 1, row 2");
	zassert_equal(px(band, 6, 0, 0), BG);
	zassert_equal(px(band, 6, 3, 0), FG, "B: column 1, row 0");
	zassert_equal(px(band, 6, 3, 8), FG, "B: row 8, from the column's second byte");
	zassert_equal(px(band, 6, 2, 8), BG);
	for (size_t y = 0; y < 9; y++) {
		zassert_equal(px(band, 6, 4, y), BG, "past the text: background");
		zassert_equal(px(band, 6, 5, y), BG);
	}
}

/**
 * @brief Characters the font lacks show as '?'; a UTF-8 character takes
 *        one place; text is cut at the band's edge.
 */
ZTEST(meter_raster, test_fallbacks)
{
	uint16_t band[5 * 9];

	raster_text(&tiny, "z\xc3\xa9" "ABBB", band, 5, FG, BG);
	zassert_equal(px(band, 5, 0, 0), FG, "'z' drawn as '?'");
	zassert_equal(px(band, 5, 0, 8), FG);
	zassert_equal(px(band, 5, 2, 0), FG, "the UTF-8 'e acute' as one '?'");
	zassert_equal(px(band, 5, 4, 1), FG, "'A' cut to its first column");
}

/** @brief An MSB-first font reads bit 7 as the top pixel. */
ZTEST(meter_raster, test_msb_first)
{
	static const uint8_t glyph[1][2] = { { 0x80, 0x00 } };
	const struct cfb_font f = {
		.data = glyph,
		/* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange,bugprone-signed-bitwise): Zephyr's font caps are OR-ed enum flags */
		.caps = CFB_FONT_MONO_VPACKED | CFB_FONT_MSB_FIRST,
		.width = 1,
		.height = 9,
		.first_char = 'X',
		.last_char = 'X',
	};
	uint16_t band[1 * 9];

	raster_text(&f, "?", band, 1, FG, BG);
	zassert_equal(px(band, 1, 0, 0), FG, "no '?': the first character instead");
	zassert_equal(px(band, 1, 0, 7), BG);
}

ZTEST_SUITE(meter_raster, NULL, NULL, NULL, NULL, NULL);
