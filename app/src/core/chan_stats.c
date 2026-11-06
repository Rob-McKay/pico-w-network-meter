/*
 * Copyright (c) 2026 Rob McKay
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "chan_stats.h"

/* Width of a 2.4 GHz channel (802.11g/n, 20 MHz), MHz. */
#define CHAN_WIDTH_MHZ     20
/* The unit of power, dBm: 10 dB below the weakest value converted, so that
 * every whole dBm from -130 up converts back exactly.
 */
#define POWER_FLOOR_DBM    (-140)
/* Weakest value chan_dbm_to_power() takes, dBm. */
#define DBM_MIN            (-130)
/* Fixed-point scale of the mantissa tables: 10^MANTISSA_DIGITS. */
#define MANTISSA_ONE       10000U
#define MANTISSA_DIGITS    4U
/* Steps of 1 dB in a decade (10 dB). */
#define DB_PER_DECADE      10
/* Smallest buffer a name is shortened in the middle for: "ab..yz". */
#define MIDDLE_CUT_MIN     7U
/* Interference thresholds of chan_stats_level(), dBm. */
#define LEVEL_MODERATE_DBM (-85)
#define LEVEL_BUSY_DBM     (-70)

/* The standard, non-overlapping channels, in order. */
static const uint8_t standard_channels[] = {1, 6, 11};

/* The 5 GHz 20 MHz channels, four to an 80 MHz block, in order. */
static const uint8_t channels_5g[CHAN_5G_COUNT] = {
	36,  40,  44,  48,  52,  56,  60,  64,  100, 104, 108, 112, 116, 120,
	124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177,
};

/* The DFS (radar detection) 5 GHz channels: 52-64 and 100-144. */
#define DFS_LOW_FIRST  52
#define DFS_LOW_LAST   64
#define DFS_HIGH_FIRST 100
#define DFS_HIGH_LAST  144

/* 10^(r/10) for r = 0..9, times MANTISSA_ONE. */
static const uint32_t db_mantissa[DB_PER_DECADE] = {
	10000, 12589, 15849, 19953, 25119, 31623, 39811, 50119, 63096, 79433,
};

/* 10^((r+0.5)/10) for r = 0..9, times MANTISSA_ONE: where rounding to the
 * nearest whole dB moves from r to r+1.
 */
static const uint32_t db_round_up[DB_PER_DECADE] = {
	11220, 14125, 17783, 22387, 28184, 35481, 44668, 56234, 70795, 89125,
};

/**
 * @brief Raise 10 to a power.
 *
 * @param exp Exponent, 0-19.
 * @return 10^exp.
 *
 * @sideeffects None; errno unchanged.
 */
static uint64_t pow10_u64(unsigned int exp)
{
	uint64_t v = 1;

	for (unsigned int i = 0; i < exp; i++) {
		v *= 10U;
	}

	return v;
}

/**
 * @brief Centre frequency of a 2.4 GHz channel.
 *
 * @param channel CHAN_FIRST..CHAN_LAST.
 * @return MHz.
 *
 * @sideeffects None; errno unchanged.
 */
static int centre_mhz(int channel)
{
	return channel == 14 ? 2484 : 2407 + (5 * channel);
}

/**
 * @brief How much of a network's channel overlaps another channel.
 *
 * @param a One channel.
 * @param b The other.
 * @return Overlap in MHz: CHAN_WIDTH_MHZ for the same channel, down to 0.
 *
 * @sideeffects None; errno unchanged.
 */
static int overlap_mhz(int a, int b)
{
	int df = centre_mhz(a) - centre_mhz(b);

	if (df < 0) {
		df = -df;
	}

	return df >= CHAN_WIDTH_MHZ ? 0 : CHAN_WIDTH_MHZ - df;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * Integer only: 10^(x/10) is 10^(x div 10) times a mantissa from db_mantissa[], rounded. The
 * unit (POWER_FLOOR_DBM, -140 dBm) is 10 dB below the weakest input, so that every whole dBm
 * from -130 converts back exactly.
 */
uint64_t chan_dbm_to_power(int dbm)
{
	if (dbm < DBM_MIN) {
		dbm = DBM_MIN;
	} else if (dbm > 0) {
		dbm = 0;
	}

	unsigned int x = (unsigned int)(dbm - POWER_FLOOR_DBM);
	uint64_t scaled = pow10_u64(x / DB_PER_DECADE) * db_mantissa[x % DB_PER_DECADE];

	return (scaled + (MANTISSA_ONE / 2U)) / MANTISSA_ONE;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * Finds the power's decade, then its mantissa in MANTISSA_ONE units (dividing down rather than
 * multiplying up above 10^4, so large sums can't overflow 64 bits), and rounds to the nearest
 * dB with db_round_up[].
 */
int chan_power_to_dbm(uint64_t power)
{
	if (power == 0U) {
		return CHAN_RSSI_NONE;
	}

	unsigned int decade = 0;

	while (power / pow10_u64(decade) >= 10U) {
		decade++;
	}

	/* power / 10^decade is in [1, 10): its mantissa, in MANTISSA_ONE units,
	 * worked out without overflowing for powers above 10^15.
	 */
	uint64_t mantissa = decade >= MANTISSA_DIGITS ? power / pow10_u64(decade - MANTISSA_DIGITS)
						      : power * pow10_u64(MANTISSA_DIGITS - decade);
	unsigned int step = 0;

	while (step < DB_PER_DECADE && mantissa >= db_round_up[step]) {
		step++;
	}

	return POWER_FLOOR_DBM + (int)((decade * DB_PER_DECADE) + step);
}

/*
 * Public interface: documented in chan_stats.h.
 */
int chan_5g_index(uint8_t channel)
{
	for (size_t i = 0; i < ARRAY_SIZE(channels_5g); i++) {
		if (channels_5g[i] == channel) {
			return (int)i;
		}
	}

	return -1;
}

/*
 * Public interface: documented in chan_stats.h.
 */
uint8_t chan_5g_channel(size_t index)
{
	return index < ARRAY_SIZE(channels_5g) ? channels_5g[index] : 0U;
}

/*
 * Public interface: documented in chan_stats.h.
 */
bool chan_5g_dfs(uint8_t channel)
{
	return (channel >= DFS_LOW_FIRST && channel <= DFS_LOW_LAST) ||
	       (channel >= DFS_HIGH_FIRST && channel <= DFS_HIGH_LAST);
}

/**
 * @brief Find the 80 MHz block a 5 GHz channel belongs to.
 *
 * @param channel Channel number.
 * @return 0 (36-48) to CHAN_5G_BLOCKS - 1 (165-177), or -1 if it isn't a
 *         5 GHz channel the meter knows.
 *
 * @sideeffects None; errno unchanged.
 */
static int block_5g(uint8_t channel)
{
	int i = chan_5g_index(channel);

	return i < 0 ? -1 : i / CHAN_5G_BLOCK_CHANNELS;
}

/**
 * @brief Give an 80 MHz block's first channel.
 *
 * @param block 0 (36-48) to CHAN_5G_BLOCKS - 1 (165-177).
 * @return 36, 52, 100, 116, 132, 149 or 165.
 *
 * @sideeffects None; errno unchanged.
 */
static uint8_t block_first(int block)
{
	return channels_5g[(size_t)block * CHAN_5G_BLOCK_CHANNELS];
}

/**
 * @brief Tell whether an access point's channel is one of its band's.
 *
 * @param ap The access point.
 * @return True for 2.4 GHz channels CHAN_FIRST..CHAN_LAST and the 5 GHz
 *         channels chan_5g_index() knows.
 *
 * @sideeffects None; errno unchanged.
 */
static bool on_known_channel(const struct chan_ap *ap)
{
	switch (ap->band) {
	case CHAN_BAND_2G4:
		return ap->channel >= CHAN_FIRST && ap->channel <= CHAN_LAST;
	case CHAN_BAND_5G:
		return chan_5g_index(ap->channel) >= 0;
	default:
		return false;
	}
}

/*
 * Public interface: documented in chan_stats.h.
 */
void chan_stats_reset(struct chan_stats *s)
{
	memset(s, 0, sizeof(*s));
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * A linear search by BSSID: a scan holds at most CHAN_MAX_APS access points.
 */
int chan_stats_add(struct chan_stats *s, const struct chan_ap *ap)
{
	if (!on_known_channel(ap)) {
		s->ignored++;
		return -EINVAL;
	}

	for (size_t i = 0; i < s->ap_count; i++) {
		if (memcmp(s->aps[i].bssid, ap->bssid, CHAN_BSSID_LEN) == 0) {
			if (ap->rssi > s->aps[i].rssi) {
				s->aps[i] = *ap;
			}
			return 0;
		}
	}

	if (s->ap_count >= CHAN_MAX_APS) {
		s->dropped++;
		return -ENOSPC;
	}
	s->aps[s->ap_count++] = *ap;

	return 0;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * Adds up every access point's power in mW (not dBm), each weighted by its overlap with the
 * channel (overlap_mhz()) over CHAN_WIDTH_MHZ. The strongest network on the channel counts in
 * full (an overlap of CHAN_WIDTH_MHZ), so taking its power off the total leaves the others'
 * exactly.
 */
void chan_stats_channel(const struct chan_stats *s, uint8_t channel, struct chan_info *out)
{
	uint64_t power = 0;
	uint64_t best_power = 0;

	out->channel = channel;
	out->ap_count = 0;
	out->best_rssi = CHAN_RSSI_NONE;
	out->freshness = 0;

	for (size_t i = 0; i < s->ap_count; i++) {
		const struct chan_ap *ap = &s->aps[i];

		if (ap->band != CHAN_BAND_2G4) {
			continue;
		}
		int overlap = overlap_mhz(ap->channel, channel);
		uint64_t share =
			(chan_dbm_to_power(ap->rssi) * (unsigned int)overlap) / CHAN_WIDTH_MHZ;

		if (ap->channel == channel) {
			out->ap_count++;
			if (ap->rssi > out->best_rssi) {
				out->best_rssi = ap->rssi;
				best_power = share;
			}
		}
		if (overlap > 0 && ap->freshness > out->freshness) {
			out->freshness = ap->freshness;
		}
		power += share;
	}

	out->interference = (int16_t)chan_power_to_dbm(power);
	out->others = (int16_t)chan_power_to_dbm(power - best_power);
}

/*
 * Public interface: documented in chan_stats.h.
 */
size_t chan_stats_band_count(const struct chan_stats *s, enum chan_band band)
{
	size_t n = 0;

	for (size_t i = 0; i < s->ap_count; i++) {
		if (s->aps[i].band == (uint8_t)band) {
			n++;
		}
	}

	return n;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * A network's RSSI is measured on its 20 MHz primary channel, which stands
 * for its power on each 20 MHz of its block, so it counts at full weight on
 * every channel of the block.
 */
void chan_stats_channel_5g(const struct chan_stats *s, uint8_t channel, struct chan_info *out)
{
	uint64_t power = 0;
	uint64_t best_power = 0;
	int block = block_5g(channel);

	out->channel = channel;
	out->ap_count = 0;
	out->best_rssi = CHAN_RSSI_NONE;
	out->freshness = 0;
	for (size_t i = 0; block >= 0 && i < s->ap_count; i++) {
		const struct chan_ap *ap = &s->aps[i];

		if (ap->band != CHAN_BAND_5G || block_5g(ap->channel) != block) {
			continue;
		}
		uint64_t share = chan_dbm_to_power(ap->rssi);

		if (ap->channel == channel) {
			out->ap_count++;
			if (ap->rssi > out->best_rssi) {
				out->best_rssi = ap->rssi;
				best_power = share;
			}
		}
		if (ap->freshness > out->freshness) {
			out->freshness = ap->freshness;
		}
		power += share;
	}
	out->interference = (int16_t)chan_power_to_dbm(power);
	out->others = (int16_t)chan_power_to_dbm(power - best_power);
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * The strict comparisons keep the earlier candidate on a full tie, which makes the lower
 * channel win.
 */
uint8_t chan_stats_recommend(const struct chan_stats *s)
{
	struct chan_info best;

	chan_stats_channel(s, standard_channels[0], &best);
	for (size_t i = 1; i < ARRAY_SIZE(standard_channels); i++) {
		struct chan_info c;

		chan_stats_channel(s, standard_channels[i], &c);
		if (c.interference < best.interference ||
		    (c.interference == best.interference && c.ap_count < best.ap_count)) {
			best = c;
		}
	}

	return best.channel;
}

/**
 * @brief Count the 5 GHz networks in an 80 MHz block.
 *
 * @param s Statistics.
 * @param block 0 to CHAN_5G_BLOCKS - 1.
 * @return Networks whose primary channel is in the block.
 *
 * @sideeffects None; errno unchanged.
 */
static size_t block_networks(const struct chan_stats *s, int block)
{
	size_t n = 0;

	for (size_t i = 0; i < s->ap_count; i++) {
		if (s->aps[i].band == CHAN_BAND_5G && block_5g(s->aps[i].channel) == block) {
			n++;
		}
	}

	return n;
}

/**
 * @brief Tell whether one 80 MHz block is a better choice than another.
 *
 * @param s Statistics.
 * @param a The candidate block, 0 to CHAN_5G_BLOCKS - 1.
 * @param b The best block so far, of a lower number.
 * @return True if @p a has less interference; on a tie, fewer networks;
 *         then if @p a is free of DFS and @p b isn't.
 *
 * @sideeffects None; errno unchanged.
 */
static bool block_better(const struct chan_stats *s, int a, int b)
{
	uint8_t first_a = block_first(a);
	uint8_t first_b = block_first(b);
	struct chan_info ia;
	struct chan_info ib;

	chan_stats_channel_5g(s, first_a, &ia);
	chan_stats_channel_5g(s, first_b, &ib);
	if (ia.interference != ib.interference) {
		return ia.interference < ib.interference;
	}
	size_t na = block_networks(s, a);
	size_t nb = block_networks(s, b);

	if (na != nb) {
		return na < nb;
	}

	return !chan_5g_dfs(first_a) && chan_5g_dfs(first_b);
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * Blocks are tried lowest first, and a later one wins only if it is
 * strictly better, so a full tie goes to the lower block.
 */
uint8_t chan_stats_recommend_5g(const struct chan_stats *s, uint32_t blocks)
{
	int best = -1;

	for (int b = 0; b < CHAN_5G_BLOCKS; b++) {
		if ((blocks & BIT(b)) == 0U) {
			continue;
		}
		if (best < 0 || block_better(s, b, best)) {
			best = b;
		}
	}

	return best < 0 ? 0U : block_first(best);
}

/*
 * Public interface: documented in chan_stats.h.
 */
uint32_t chan_5g_blocks(bool with_132, bool with_upper)
{
	uint32_t blocks = BIT(0) | BIT(1) | BIT(2) | BIT(3);

	if (with_132) {
		blocks |= BIT(4);
	}
	if (with_upper) {
		blocks |= BIT(5);
	}

	return blocks;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * An insertion sort straight into the output array, keeping only as many entries as it
 * has room for: a scan holds at most CHAN_MAX_APS access points.
 */
size_t chan_stats_strongest(const struct chan_stats *s, enum chan_band band,
			    const struct chan_ap **out, size_t max)
{
	size_t n = 0;

	/* Insertion sort into out[]: a scan has at most CHAN_MAX_APS. */
	for (size_t i = 0; i < s->ap_count; i++) {
		const struct chan_ap *ap = &s->aps[i];

		if (ap->band != (uint8_t)band) {
			continue;
		}
		size_t pos = n;

		while (pos > 0 && out[pos - 1]->rssi < ap->rssi) {
			pos--;
		}
		if (pos >= max) {
			continue;
		}
		for (size_t j = (n < max ? n : max - 1); j > pos; j--) {
			out[j] = out[j - 1];
		}
		out[pos] = ap;
		if (n < max) {
			n++;
		}
	}

	return n;
}

/**
 * @brief Compare two access point records field by field.
 *
 * Field by field rather than memcmp(), which would also compare padding.
 *
 * @param a One record.
 * @param b The other.
 * @return True if every field is equal.
 *
 * @sideeffects None; errno unchanged.
 */
static bool chan_ap_equal(const struct chan_ap *a, const struct chan_ap *b)
{
	return strcmp(a->ssid, b->ssid) == 0 && memcmp(a->bssid, b->bssid, CHAN_BSSID_LEN) == 0 &&
	       a->channel == b->channel && a->band == b->band && a->rssi == b->rssi &&
	       a->secured == b->secured && a->freshness == b->freshness;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * Field by field (chan_ap_equal()), not memcmp(), which would also compare padding.
 */
bool chan_stats_equal(const struct chan_stats *a, const struct chan_stats *b)
{
	if (a->ap_count != b->ap_count || a->ignored != b->ignored || a->dropped != b->dropped) {
		return false;
	}
	for (size_t i = 0; i < a->ap_count; i++) {
		if (!chan_ap_equal(&a->aps[i], &b->aps[i])) {
			return false;
		}
	}

	return true;
}

/*
 * Public interface: documented in chan_stats.h.
 */
enum chan_level chan_stats_level(int interference)
{
	if (interference >= LEVEL_BUSY_DBM) {
		return CHAN_LEVEL_BUSY;
	}
	if (interference >= LEVEL_MODERATE_DBM) {
		return CHAN_LEVEL_MODERATE;
	}

	return CHAN_LEVEL_CLEAR;
}

/*
 * Public interface: documented in chan_stats.h.
 */
const char *chan_level_name(enum chan_level level)
{
	static const char *const names[] = {
		[CHAN_LEVEL_CLEAR] = "clear",
		[CHAN_LEVEL_MODERATE] = "moderate",
		[CHAN_LEVEL_BUSY] = "busy",
	};

	return (size_t)level < ARRAY_SIZE(names) ? names[level] : "?";
}

/*
 * Public interface: documented in chan_stats.h.
 */
bool chan_is_standard(uint8_t channel)
{
	for (size_t i = 0; i < ARRAY_SIZE(standard_channels); i++) {
		if (standard_channels[i] == channel) {
			return true;
		}
	}

	return false;
}

/*
 * Public interface: documented in chan_stats.h.
 *
 * An SSID of NULs ends up empty here: the scanner copies it as a C string.
 */
bool chan_ap_hidden(const struct chan_ap *ap)
{
	return ap->ssid[0] == '\0';
}

/**
 * @brief Copy a name, shortened in the middle if it is too long.
 *
 * The start gets the odd character when the room left is odd. Into fewer
 * than MIDDLE_CUT_MIN bytes, the name is cut at the end instead: too
 * little would be left either side of "..".
 *
 * @param name The name.
 * @param[out] buf The copy, always terminated.
 * @param size Size of @p buf, at least 1.
 *
 * @sideeffects None (writes only through @p buf); errno may be set by snprintf() (only on an
 *              output error).
 */
static void copy_middle_cut(const char *name, char *buf, size_t size)
{
	size_t len = strlen(name);
	size_t room = size - 1U;

	if (len <= room || size < MIDDLE_CUT_MIN) {
		(void)snprintf(buf, size, "%s", name);
		return;
	}

	size_t tail = (room - 2U) / 2U;
	size_t head = room - 2U - tail;

	memcpy(buf, name, head);
	buf[head] = '.';
	buf[head + 1U] = '.';
	memcpy(&buf[head + 2U], &name[len - tail], tail);
	buf[room] = '\0';
}

/*
 * Public interface: documented in chan_stats.h.
 */
void chan_ap_name(const struct chan_ap *ap, char *buf, size_t size)
{
	if (size == 0U) {
		return;
	}
	if (!chan_ap_hidden(ap)) {
		copy_middle_cut(ap->ssid, buf, size);
		return;
	}
	(void)snprintf(buf, size, "hidden %02x:%02x:%02x:%02x:%02x:%02x",
		       (unsigned int)ap->bssid[0], (unsigned int)ap->bssid[1],
		       (unsigned int)ap->bssid[2], (unsigned int)ap->bssid[3],
		       (unsigned int)ap->bssid[4], (unsigned int)ap->bssid[5]);
}
