/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unit tests for the network meter's hardware-independent code: the
 * channel statistics (chan_stats.c) and the text rasteriser
 * (text_raster.c).
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ap_history.h"
#include "boot_record.h"
#include "chan_stats.h"
#include "paging.h"
#include "rgb565.h"
#include "scan_health.h"
#include "text_raster.h"

/* ---- Channel statistics -------------------------------------------------- */

/**
 * @brief Make an access point record.
 *
 * @param id Distinguishes BSSIDs (the last byte) and names ("ap<id>").
 * @param channel Channel.
 * @param rssi dBm.
 * @return The record.
 *
 * @sideeffects None. errno may be set by snprintf() (only on an output error).
 */
static struct chan_ap make_ap(uint8_t id, uint8_t channel, int8_t rssi)
{
	struct chan_ap ap = {
		.bssid = {0x02, 0x00, 0x00, 0x00, 0x00, id},
		.channel = channel,
		.rssi = rssi,
		.secured = true,
		.freshness = CHAN_FRESH,
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
 *
 * @sideeffects None (writes only through @p s). errno may be set by snprintf() (only on an
 *              output error).
 */
static void add_ap(struct chan_stats *s, uint8_t id, uint8_t channel, int8_t rssi)
{
	struct chan_ap ap = make_ap(id, channel, rssi);

	zassert_ok(chan_stats_add(s, &ap));
}

/**
 * @brief dBm to linear power and back gives the same whole dBm, -130 to 0.
 *
 * @sideeffects None; errno unchanged.
 */
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

/**
 * @brief Twice the power is 3 dB more; ten times is 10 dB.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_chan, test_power_sums)
{
	uint64_t p = chan_dbm_to_power(-60);

	zassert_equal(chan_power_to_dbm(p * 2U), -57);
	zassert_equal(chan_power_to_dbm(p * 10U), -50);
}

/**
 * @brief Every network at 0 dBm on one channel: a large sum still converts.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief A BSSID heard twice is kept once, at its stronger signal.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief Results off the 2.4 GHz channels are counted, not kept.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief Past CHAN_MAX_APS access points, the rest are counted as dropped.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
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

/**
 * @brief The others figure counts every network but the channel's
 *        strongest: others on the same channel, and spill-over from
 *        overlapping channels.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
ZTEST(meter_chan, test_channel_others)
{
	static struct chan_stats s;
	struct chan_info info;

	chan_stats_reset(&s);
	add_ap(&s, 1, 6, -50);
	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.interference, -50);
	zassert_equal(info.others, CHAN_RSSI_NONE,
		      "the only network doesn't interfere with itself");
	chan_stats_channel(&s, 7, &info);
	zassert_equal(info.others, info.interference, "no network on 7: all of it interferes");
	zassert_equal(info.others, -51, "6's network at 3/4");

	add_ap(&s, 2, 6, -60);
	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.best_rssi, -50);
	zassert_equal(info.others, -60, "the weaker network on the same channel interferes");

	add_ap(&s, 3, 7, -50);
	chan_stats_channel(&s, 7, &info);
	zassert_equal(info.best_rssi, -50);
	zassert_equal(info.others, -51, "6's networks: -50 and -60 at 3/4");
	zassert_equal(info.interference, -47, "7's own network adds to the total");
	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.others, -51, "-60 on 6, and -50 spilling from 7 at 3/4");
}

/**
 * @brief Channel 14 sits 12 MHz above 13, not 5.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief Signals on one channel add up; the strongest is reported.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief The quietest of 1, 6 and 11 is recommended; ties go to fewer networks, then 1.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief Networks are listed strongest first, cut to the room given.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
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

/**
 * @brief Interference levels split at -85 and -70 dBm.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_chan, test_level)
{
	zassert_equal(chan_stats_level(CHAN_RSSI_NONE), CHAN_LEVEL_CLEAR);
	zassert_equal(chan_stats_level(-86), CHAN_LEVEL_CLEAR);
	zassert_equal(chan_stats_level(-85), CHAN_LEVEL_MODERATE);
	zassert_equal(chan_stats_level(-71), CHAN_LEVEL_MODERATE);
	zassert_equal(chan_stats_level(-70), CHAN_LEVEL_BUSY);
	zassert_equal(chan_stats_level(-20), CHAN_LEVEL_BUSY);
}

/**
 * @brief A channel is as fresh as the freshest network reaching it.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
ZTEST(meter_chan, test_channel_freshness)
{
	static struct chan_stats s;
	struct chan_ap old = make_ap(1, 6, -50);
	struct chan_ap fresh = make_ap(2, 8, -70);
	struct chan_info info;

	chan_stats_reset(&s);
	old.freshness = 100;
	fresh.freshness = 200;
	zassert_ok(chan_stats_add(&s, &old));
	zassert_ok(chan_stats_add(&s, &fresh));

	chan_stats_channel(&s, 6, &info);
	zassert_equal(info.freshness, 200, "channel 8 overlaps 6 by 10 MHz");
	chan_stats_channel(&s, 3, &info);
	zassert_equal(info.freshness, 100, "only channel 6 reaches 3");
	chan_stats_channel(&s, 13, &info);
	zassert_equal(info.freshness, 0, "nothing reaches 13");
}

/**
 * @brief Equal statistics compare equal; any field that differs makes them unequal.
 *
 * @sideeffects Overwrites the test's own static statistics. errno may be set by snprintf()
 *              (only on an output error).
 */
ZTEST(meter_chan, test_equal)
{
	static struct chan_stats a;
	static struct chan_stats b;

	chan_stats_reset(&a);
	add_ap(&a, 1, 6, -60);
	add_ap(&a, 2, 11, -70);
	b = a;
	zassert_true(chan_stats_equal(&a, &b));

	b.aps[1].freshness = 100;
	zassert_false(chan_stats_equal(&a, &b), "freshness");
	b = a;
	b.aps[0].rssi = -61;
	zassert_false(chan_stats_equal(&a, &b), "rssi");
	b = a;
	b.ignored = 1;
	zassert_false(chan_stats_equal(&a, &b), "ignored count");
	b = a;
	b.aps[1].ssid[0] = 'x';
	zassert_false(chan_stats_equal(&a, &b), "ssid");
	b = a;
	b.ap_count = 1;
	zassert_false(chan_stats_equal(&a, &b), "fewer networks");
}

/**
 * @brief Only 1, 6 and 11 are standard channels.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_chan, test_standard_channels)
{
	for (uint8_t ch = 0; ch <= CHAN_LAST + 1U; ch++) {
		zassert_equal(chan_is_standard(ch), ch == 1U || ch == 6U || ch == 11U, "ch %u",
			      (unsigned int)ch);
	}
}

/**
 * @brief A network is named by its SSID; a hidden one by the end of its
 *        BSSID, which fits the networks screen's 15-character column.
 *
 * @sideeffects None. errno may be set by snprintf() (only on an output error).
 */
ZTEST(meter_chan, test_ap_name)
{
	struct chan_ap ap = make_ap(0xab, 6, -50);
	char name[CHAN_SSID_MAX + 1];

	zassert_false(chan_ap_hidden(&ap));
	chan_ap_name(&ap, name, sizeof(name));
	zassert_str_equal(name, "ap171");

	ap.ssid[0] = '\0';
	ap.bssid[3] = 0x12;
	ap.bssid[4] = 0xcd;
	zassert_true(chan_ap_hidden(&ap));
	chan_ap_name(&ap, name, sizeof(name));
	zassert_str_equal(name, "hidden 12:cd:ab");

	chan_ap_name(&ap, name, 7);
	zassert_str_equal(name, "hidden", "cut to fit, terminated");

	(void)snprintf(ap.ssid, sizeof(ap.ssid), "%s", "JurassicFibre_UID35630");
	chan_ap_name(&ap, name, 16);
	zassert_str_equal(name, "Jurassi..D35630", "too long: cut in the middle, end kept");
	chan_ap_name(&ap, name, 23);
	zassert_str_equal(name, "JurassicFibre_UID35630", "fits: whole");
	chan_ap_name(&ap, name, 22);
	zassert_str_equal(name, "JurassicFi.._UID35630", "one short: the start gets the odd one");
	chan_ap_name(&ap, name, 7);
	zassert_str_equal(name, "Ju..30", "smallest middle cut");
	chan_ap_name(&ap, name, 6);
	zassert_str_equal(name, "Juras", "too small for that: cut at the end");
	name[0] = 'x';
	chan_ap_name(&ap, name, 0);
	zassert_equal(name[0], 'x', "size 0 writes nothing");
}

ZTEST_SUITE(meter_chan, NULL, NULL, NULL, NULL, NULL);

/* ---- History of access points -------------------------------------------- */

static const struct ap_fade fade = {.start_ms = 15000, .end_ms = 30000};

/**
 * @brief Make a one-network scan.
 *
 * @param[out] scan The scan.
 * @param id See make_ap().
 * @param channel Channel.
 * @param rssi dBm.
 *
 * @sideeffects None (writes only through @p scan). errno may be set by snprintf() (only on an
 *              output error).
 */
static void one_scan(struct chan_stats *scan, uint8_t id, uint8_t channel, int8_t rssi)
{
	chan_stats_reset(scan);
	add_ap(scan, id, channel, rssi);
}

/**
 * @brief Full brightness until the fade starts, then a straight line to 0.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_history, test_freshness_curve)
{
	zassert_equal(ap_history_freshness(0, &fade), CHAN_FRESH);
	zassert_equal(ap_history_freshness(15000, &fade), CHAN_FRESH);
	zassert_equal(ap_history_freshness(22500, &fade), 127);
	zassert_equal(ap_history_freshness(29999, &fade), 0);
	zassert_equal(ap_history_freshness(30000, &fade), 0);
	zassert_equal(ap_history_freshness(60000, &fade), 0);
}

/** @brief A fade that ends when it starts (or before) drops straight to 0, without dividing by 0.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_history, test_freshness_no_fade)
{
	static const struct ap_fade none = {.start_ms = 5000, .end_ms = 5000};
	static const struct ap_fade backwards = {.start_ms = 5000, .end_ms = 1000};

	zassert_equal(ap_history_freshness(5000, &none), CHAN_FRESH);
	zassert_equal(ap_history_freshness(5001, &none), 0);
	zassert_equal(ap_history_freshness(4000, &backwards), CHAN_FRESH);
	zassert_equal(ap_history_freshness(5001, &backwards), 0);
}

/**
 * @brief An empty scan leaves the networks heard before on show.
 *
 * @sideeffects Overwrites the test's own static history and scans. errno may be set by
 *              snprintf() (only on an output error).
 */
ZTEST(meter_history, test_empty_scan_keeps_data)
{
	static struct ap_history h;
	static struct chan_stats scan;
	static struct chan_stats snap;

	ap_history_reset(&h);
	one_scan(&scan, 1, 11, -60);
	ap_history_merge(&h, &scan, 1000);
	chan_stats_reset(&scan);
	ap_history_merge(&h, &scan, 11000);

	ap_history_snapshot(&h, 11000, &fade, &snap);
	zassert_equal(snap.ap_count, 1);
	zassert_equal(snap.aps[0].channel, 11);
	zassert_equal(snap.aps[0].freshness, CHAN_FRESH, "10 s old: not fading yet");
}

/**
 * @brief Networks fade from 15 s and are gone at 30 s; hearing one again restores it.
 *
 * @sideeffects Overwrites the test's own static history and scans. errno may be set by
 *              snprintf() (only on an output error).
 */
ZTEST(meter_history, test_fade_and_expire)
{
	static struct ap_history h;
	static struct chan_stats scan;
	static struct chan_stats snap;

	ap_history_reset(&h);
	one_scan(&scan, 1, 1, -60);
	ap_history_merge(&h, &scan, 0);
	one_scan(&scan, 2, 6, -70);
	ap_history_merge(&h, &scan, 20000);

	ap_history_snapshot(&h, 25000, &fade, &snap);
	zassert_equal(snap.ap_count, 2);
	zassert_equal(snap.aps[0].freshness, 85, "25 s old: a third left");
	zassert_equal(snap.aps[1].freshness, CHAN_FRESH);

	ap_history_expire(&h, 30000, &fade);
	zassert_equal(h.count, 1, "30 s old: forgotten");
	zassert_equal(h.aps[0].channel, 6);

	one_scan(&scan, 2, 7, -65);
	ap_history_merge(&h, &scan, 40000);
	ap_history_snapshot(&h, 40000, &fade, &snap);
	zassert_equal(snap.ap_count, 1, "the same BSSID, updated");
	zassert_equal(snap.aps[0].channel, 7);
	zassert_equal(snap.aps[0].rssi, -65);
	zassert_equal(snap.aps[0].freshness, CHAN_FRESH);
}

/**
 * @brief When full, a new network replaces the one heard longest ago.
 *
 * @sideeffects Overwrites the test's own static history and scans. errno may be set by
 *              snprintf() (only on an output error).
 */
ZTEST(meter_history, test_full_replaces_oldest)
{
	static struct ap_history h;
	static struct chan_stats scan;

	ap_history_reset(&h);
	for (uint8_t i = 0; i < CHAN_MAX_APS; i++) {
		one_scan(&scan, i, 1, -60);
		ap_history_merge(&h, &scan, 1000 + i);
	}
	one_scan(&scan, 200, 6, -50);
	ap_history_merge(&h, &scan, 5000);
	zassert_equal(h.count, CHAN_MAX_APS);
	zassert_equal(h.aps[0].bssid[5], 200, "id 0, the oldest, replaced");
	zassert_equal(h.heard_ms[0], 5000);
}

/**
 * @brief The latest scan's ignored and dropped counts are reported.
 *
 * @sideeffects Overwrites the test's own static history and scans. errno unchanged.
 */
ZTEST(meter_history, test_counts)
{
	static struct ap_history h;
	static struct chan_stats scan;
	static struct chan_stats snap;

	ap_history_reset(&h);
	chan_stats_reset(&scan);
	scan.ignored = 3;
	scan.dropped = 1;
	ap_history_merge(&h, &scan, 0);
	ap_history_snapshot(&h, 0, &fade, &snap);
	zassert_equal(snap.ignored, 3);
	zassert_equal(snap.dropped, 1);
}

ZTEST_SUITE(meter_history, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Fading scales each colour component; 255 changes nothing, 0 is black.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_raster, test_fade)
{
	uint16_t orange = rgb565x(255, 128, 64);

	zassert_equal(rgb565x_fade(orange, 255), orange);
	zassert_equal(rgb565x_fade(orange, 0), 0);
	/* Half of 31 and 63, rounded down: 15 and 31, which 120 and 124 give */
	zassert_equal(rgb565x_fade(rgb565x(255, 255, 255), 128), rgb565x(120, 124, 120));
}

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
	{0x01, 0x01, 0x00, 0x00}, /* ? */
	{0x00, 0x00, 0x00, 0x00}, /* @ */
	{0x02, 0x00, 0x04, 0x00}, /* A */
	{0x00, 0x00, 0x01, 0x01}, /* B */
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
 *
 * @sideeffects None; errno unchanged.
 */
static uint16_t px(const uint16_t *band, size_t width, size_t x, size_t y)
{
	return band[(y * width) + x];
}

/**
 * @brief RGB565, stored big-endian as the panel takes it.
 *
 * @sideeffects None; errno unchanged.
 */
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

/**
 * @brief Glyphs land where the font says, column after column.
 *
 * @sideeffects None; errno unchanged.
 */
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
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_raster, test_fallbacks)
{
	uint16_t band[5 * 9];

	/* "z", e-acute in UTF-8, then "ABBB" as \x escapes: written as letters
	 * they would be read as more hex digits of \xa9.
	 */
	raster_text(&tiny, "z\xc3\xa9\x41\x42\x42\x42", band, 5, FG, BG);
	zassert_equal(px(band, 5, 0, 0), FG, "'z' drawn as '?'");
	zassert_equal(px(band, 5, 0, 8), FG);
	zassert_equal(px(band, 5, 2, 0), FG, "the UTF-8 'e acute' as one '?'");
	zassert_equal(px(band, 5, 4, 1), FG, "'A' cut to its first column");
}

/**
 * @brief An MSB-first font reads bit 7 as the top pixel.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_raster, test_msb_first)
{
	static const uint8_t glyph[1][2] = {{0x80, 0x00}};
	const struct cfb_font f = {
		.data = glyph,
		/* Zephyr's font caps are OR-ed enum flags. */
		/* clang-format off */
		/* NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange,bugprone-signed-bitwise) */
		.caps = CFB_FONT_MONO_VPACKED | CFB_FONT_MSB_FIRST,
		/* clang-format on */
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

/**
 * @brief Bold text inks every pixel of the plain text and the one to its
 *        right, but never across into the next character's cell.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_raster, test_bold)
{
	uint16_t plain[6 * 9];
	uint16_t bold[6 * 9];
	unsigned int extra = 0;

	raster_text(&tiny, "AB", plain, 6, FG, BG);
	raster_text_bold(&tiny, "AB", bold, 6, FG, BG);
	for (size_t y = 0; y < 9; y++) {
		for (size_t x = 0; x < 6; x++) {
			bool left_ink = (x % tiny.width) != 0U && px(plain, 6, x - 1U, y) == FG;
			uint16_t want = (px(plain, 6, x, y) == FG || left_ink) ? FG : BG;

			zassert_equal(px(bold, 6, x, y), want, "x %zu, y %zu", x, y);
			extra += (px(bold, 6, x, y) != px(plain, 6, x, y)) ? 1U : 0U;
		}
	}
	zassert_true(extra > 0U, "bold differs from plain");
}

ZTEST_SUITE(meter_raster, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief The meter restarts only after the given number of failures in a
 *        row; a scan that works, even one that hears nothing, starts the
 *        count again.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_health, test_failures_in_a_row)
{
	struct scan_health h;

	scan_health_reset(&h);
	zassert_false(scan_health_record(&h, -EAGAIN, 3));
	zassert_false(scan_health_record(&h, -EAGAIN, 3));
	zassert_false(scan_health_record(&h, 0, 3), "a working scan ends the run");
	zassert_false(scan_health_record(&h, -EAGAIN, 3));
	zassert_false(scan_health_record(&h, -EINPROGRESS, 3));
	zassert_true(scan_health_record(&h, -EIO, 3), "third failure in a row");
	zassert_true(scan_health_record(&h, -EIO, 3), "and every one after it");
}

/**
 * @brief A limit of 0 never asks for a restart, and the count doesn't wrap
 *        round to zero.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_health, test_no_limit)
{
	struct scan_health h = {.failures = UINT_MAX - 1U};

	zassert_false(scan_health_record(&h, -EAGAIN, 0));
	zassert_false(scan_health_record(&h, -EAGAIN, 0));
	zassert_equal(h.failures, UINT_MAX, "stops at UINT_MAX");
	zassert_true(scan_health_record(&h, -EAGAIN, 5), "still over a limit");
}

ZTEST_SUITE(meter_health, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief Paging moves a page at a time, and stops at the first and last
 *        pages.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_paging, test_move)
{
	zassert_equal(paging_move(0, true, 25, 10), 10);
	zassert_equal(paging_move(10, true, 25, 10), 20);
	zassert_equal(paging_move(20, true, 25, 10), 20, "20-24 is the last page");
	zassert_equal(paging_move(20, false, 25, 10), 10);
	zassert_equal(paging_move(0, false, 25, 10), 0, "already at the top");
	zassert_equal(paging_move(0, true, 10, 10), 0, "one page: nowhere to go");
	zassert_equal(paging_move(0, true, 0, 10), 0, "empty list");
	zassert_equal(paging_move(7, true, 25, 0), 7, "no rows: unchanged");
}

/**
 * @brief A page stays within a list that shrinks, and on a page boundary.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_paging, test_clamp)
{
	zassert_equal(paging_clamp(20, 25, 10), 20);
	zassert_equal(paging_clamp(20, 15, 10), 10, "shrunk to two pages");
	zassert_equal(paging_clamp(20, 20, 10), 10, "exactly two pages");
	zassert_equal(paging_clamp(20, 0, 10), 0, "emptied");
	zassert_equal(paging_clamp(13, 25, 10), 10, "rounded to a page start");
	zassert_equal(paging_clamp(SIZE_MAX, 25, 10), 20, "no overflow");
	zassert_equal(paging_move(SIZE_MAX, true, 25, 10), 20, "no overflow moving either");
	zassert_equal(paging_clamp(5, 25, 0), 0, "no rows");
}

ZTEST_SUITE(meter_paging, NULL, NULL, NULL, NULL, NULL);

/**
 * @brief A record survives its flash format, including extreme values.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_boot, test_record_round_trip)
{
	static const int32_t details[] = {0, -11, INT32_MIN, INT32_MAX, -1};
	uint8_t bytes[BOOT_RECORD_SIZE];

	for (size_t i = 0; i < ARRAY_SIZE(details); i++) {
		struct boot_record in = {
			.boot = 0xfedcba98U,
			.uptime_s = UINT32_MAX,
			.detail = details[i],
			.arg = 0x10001234U,
			.cause = BOOT_CRASH,
		};
		struct boot_record out;

		boot_record_encode(&in, bytes);
		zassert_ok(boot_record_decode(bytes, sizeof(bytes), &out));
		zassert_equal(out.boot, in.boot);
		zassert_equal(out.uptime_s, in.uptime_s);
		zassert_equal(out.detail, in.detail, "detail %d", (int)in.detail);
		zassert_equal(out.arg, in.arg);
		zassert_equal(out.cause, in.cause);
	}
	zassert_equal(bytes[0], 1, "format version first");
	zassert_equal(bytes[4], 0x98, "little-endian");
}

/**
 * @brief Anything that isn't a record is refused: wrong size, version or
 *        cause.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_boot, test_record_refused)
{
	struct boot_record in = {.boot = 1, .cause = BOOT_POWER_ON};
	struct boot_record out;
	uint8_t bytes[BOOT_RECORD_SIZE];

	boot_record_encode(&in, bytes);
	zassert_equal(boot_record_decode(bytes, sizeof(bytes) - 1U, &out), -EINVAL);
	bytes[0] = 2;
	zassert_equal(boot_record_decode(bytes, sizeof(bytes), &out), -EINVAL, "version");
	bytes[0] = 1;
	bytes[1] = BOOT_NONE;
	zassert_equal(boot_record_decode(bytes, sizeof(bytes), &out), -EINVAL, "no cause");
	bytes[1] = BOOT_CAUSE_COUNT;
	zassert_equal(boot_record_decode(bytes, sizeof(bytes), &out), -EINVAL, "unknown cause");
	(void)memset(bytes, 0xff, sizeof(bytes));
	zassert_equal(boot_record_decode(bytes, sizeof(bytes), &out), -EINVAL, "erased flash");
}

/**
 * @brief Each kind of start is described in a line, with how long the run
 *        before it lasted.
 *
 * @sideeffects None. errno may be set by snprintf() (only on an output error).
 */
ZTEST(meter_boot, test_describe)
{
	struct boot_record r = {.boot = 12, .cause = BOOT_POWER_ON};
	char text[40];

	boot_record_describe(&r, text, sizeof(text));
	zassert_str_equal(text, "#12 power on");

	r.cause = BOOT_WIFI_RESTART;
	r.uptime_s = (2U * 3600U) + (31U * 60U) + 59U;
	boot_record_describe(&r, text, sizeof(text));
	zassert_str_equal(text, "#12 Wi-Fi restart after 2h31m");

	r.cause = BOOT_CRASH;
	r.detail = 3;
	r.uptime_s = 59;
	boot_record_describe(&r, text, sizeof(text));
	zassert_str_equal(text, "#12 crash 3 after 0m59s");

	r.cause = BOOT_WATCHDOG;
	r.detail = BOOT_WATCHED_SCAN;
	r.uptime_s = (3U * 86400U) + (4U * 3600U);
	boot_record_describe(&r, text, sizeof(text));
	zassert_str_equal(text, "#12 scan hung after 3d04h");

	r.cause = BOOT_UNEXPLAINED;
	r.uptime_s = 3600;
	boot_record_describe(&r, text, sizeof(text));
	zassert_str_equal(text, "#12 restart after 1h00m");

	boot_record_describe(&r, text, 4);
	zassert_str_equal(text, "#12", "cut to fit");
	text[0] = 'x';
	boot_record_describe(&r, text, 0);
	zassert_equal(text[0], 'x', "size 0 writes nothing");
}

/**
 * @brief A sealed note is valid; any change to it, or RAM's contents after
 *        a power cut, isn't.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_boot, test_note_valid)
{
	struct boot_note n = {
		.uptime_s = 100,
		.detail = -11,
		.arg = 5,
		.cause = BOOT_WIFI_RESTART,
	};
	struct boot_note garbage;

	boot_note_seal(&n);
	zassert_true(boot_note_valid(&n));
	n.uptime_s++;
	zassert_false(boot_note_valid(&n), "changed after sealing");
	boot_note_seal(&n);
	n.cause = BOOT_CAUSE_COUNT;
	boot_note_seal(&n);
	zassert_false(boot_note_valid(&n), "unknown cause");

	(void)memset(&garbage, 0xa5, sizeof(garbage));
	zassert_false(boot_note_valid(&garbage), "RAM after power-on");
}

/**
 * @brief A start is worked out from the note: none means power-on, one
 *        without a cause an unexplained restart, otherwise its cause.
 *
 * @sideeffects None; errno unchanged.
 */
ZTEST(meter_boot, test_from_note)
{
	struct boot_note n;
	struct boot_record r;

	(void)memset(&n, 0, sizeof(n));
	boot_record_from_note(&n, 7, &r);
	zassert_equal(r.cause, BOOT_POWER_ON, "no valid note: the power was off");
	zassert_equal(r.boot, 7);
	zassert_equal(r.uptime_s, 0);

	n = (struct boot_note){.uptime_s = 9000, .cause = BOOT_NONE};
	boot_note_seal(&n);
	boot_record_from_note(&n, 8, &r);
	zassert_equal(r.cause, BOOT_UNEXPLAINED, "running when it restarted");
	zassert_equal(r.uptime_s, 9000);

	n = (struct boot_note){
		.uptime_s = 600,
		.detail = -11,
		.arg = 5,
		.cause = BOOT_WIFI_RESTART,
	};
	boot_note_seal(&n);
	boot_record_from_note(&n, 9, &r);
	zassert_equal(r.cause, BOOT_WIFI_RESTART);
	zassert_equal(r.detail, -11);
	zassert_equal(r.arg, 5);
	zassert_equal(r.uptime_s, 600);
}

ZTEST_SUITE(meter_boot, NULL, NULL, NULL, NULL, NULL);
